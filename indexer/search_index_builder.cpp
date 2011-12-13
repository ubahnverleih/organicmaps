#include "search_index_builder.hpp"

#include "feature_utils.hpp"
#include "features_vector.hpp"
#include "search_delimiters.hpp"
#include "search_trie.hpp"
#include "search_string_utils.hpp"

#include "../defines.hpp"

#include "../platform/platform.hpp"

#include "../coding/trie_builder.hpp"
#include "../coding/writer.hpp"
#include "../coding/reader_writer_ops.hpp"

#include "../base/string_utils.hpp"
#include "../base/logging.hpp"

#include "../std/algorithm.hpp"
#include "../std/vector.hpp"


namespace
{

struct FeatureName
{
  strings::UniString m_name;
  char m_Value[5];

  FeatureName(strings::UniString const & name, signed char lang, uint32_t id, uint8_t rank)
  {
    m_name.reserve(name.size() + 1);
    m_name.push_back(static_cast<uint8_t>(lang));
    m_name.append(name.begin(), name.end());

    m_Value[0] = rank;
    uint32_t const idToWrite = SwapIfBigEndian(id);
    memcpy(&m_Value[1], &idToWrite, 4);
  }

  uint32_t GetKeySize() const { return m_name.size(); }
  uint32_t const * GetKeyData() const { return m_name.data(); }
  uint32_t GetValueSize() const { return 5; }
  void const * GetValueData() const { return &m_Value; }

  uint8_t GetRank() const { return static_cast<uint8_t>(m_Value[0]); }
  uint32_t GetOffset() const
  {
    uint32_t offset;
    memcpy(&offset, &m_Value[1], 4);
    return SwapIfBigEndian(offset);
  }

  inline bool operator < (FeatureName const & name) const
  {
    if (m_name != name.m_name)
      return m_name < name.m_name;
    if (GetRank() != name.GetRank())
      return GetRank() > name.GetRank();
    if (GetOffset() != name.GetOffset())
      return GetOffset() < name.GetOffset();
    return false;
  }

  inline bool operator == (FeatureName const & name) const
  {
    return m_name == name.m_name && 0 == memcmp(&m_Value, &name.m_Value, sizeof(m_Value));
  }
};

struct FeatureNameInserter
{
  vector<FeatureName> & m_names;
  uint32_t m_pos;
  uint32_t m_rank;

  FeatureNameInserter(vector<FeatureName> & names, uint32_t pos, uint8_t rank)
    : m_names(names), m_pos(pos), m_rank(rank) {}

  void AddToken(signed char lang, strings::UniString const & s) const
  {
    AddToken(lang, s, m_rank);
  }

  void AddToken(signed char lang, strings::UniString const & s, uint32_t rank) const
  {
    m_names.push_back(FeatureName(s, lang, m_pos, static_cast<uint8_t>(min(rank, 255U))));
  }

  bool operator()(signed char lang, string const & name) const
  {
    // m_names.push_back(FeatureName(, m_pos, m_rank));
    strings::UniString uniName = search::NormalizeAndSimplifyString(name);
    buffer_vector<strings::UniString, 32> tokens;
    SplitUniString(uniName, MakeBackInsertFunctor(tokens), search::Delimiters());
    if (tokens.size() > 30)
    {
      LOG(LWARNING, ("Name has too many tokens:", name));
      tokens.resize(30);
    }
    for (size_t i = 0; i < tokens.size(); ++i)
      AddToken(lang, tokens[i], /*i < 3 ? m_rank + 10 * (3 - i) : */m_rank);
    return true;
  }
};

struct FeatureInserter
{
  vector<FeatureName> & m_names;

  explicit FeatureInserter(vector<FeatureName> & names) : m_names(names) {}

  void operator() (FeatureType const & feature, uint64_t pos) const
  {
    // Add names of the feature.
    FeatureNameInserter f(m_names, static_cast<uint32_t>(pos), feature::GetSearchRank(feature));
    feature.ForEachNameRef(f);

    // Add names of categories of the feature.
    FeatureType::GetTypesFn getTypesFn;
    feature.ForEachTypeRef(getTypesFn);
    for (size_t i = 0; i < getTypesFn.m_size; ++i)
      f.AddToken(0, search::FeatureTypeToString(getTypesFn.m_types[i]));
  }
};

struct MaxValueCalc
{
  typedef uint8_t ValueType;

  ValueType operator() (void const * p, uint32_t size) const
  {
    ASSERT_EQUAL(size, 5, ());
    return *static_cast<uint8_t const *>(p);
  }
};

}  // unnamed namespace

void indexer::BuildSearchIndex(FeaturesVector const & featuresVector, Writer & writer)
{
  vector<FeatureName> names;
  featuresVector.ForEachOffset(FeatureInserter(names));
  sort(names.begin(), names.end());
  names.erase(unique(names.begin(), names.end()), names.end());
  trie::Build(writer, names.begin(), names.end(),
              trie::builder::MaxValueEdgeBuilder<MaxValueCalc>());
}

bool indexer::BuildSearchIndexFromDatFile(string const & datFile)
{
  try
  {
    string const tmpFile = GetPlatform().WritablePathForFile(datFile + ".search.tmp");

    {
      FilesContainerR readCont(datFile);

      feature::DataHeader header;
      header.Load(readCont.GetReader(HEADER_FILE_TAG));

      FeaturesVector featuresVector(readCont, header);

      FileWriter writer(tmpFile);
      BuildSearchIndex(featuresVector, writer);
    }

    // Write to container in reversed order.
    FilesContainerW writeCont(datFile, FileWriter::OP_WRITE_EXISTING);
    FileWriter writer = writeCont.GetWriter(SEARCH_INDEX_FILE_TAG);
    rw_ops::Reverse(FileReader(tmpFile), writer);

    FileWriter::DeleteFileX(tmpFile);
  }
  catch (Reader::Exception const & e)
  {
    LOG(LERROR, ("Error while reading file: ", e.what()));
    return false;
  }
  catch (Writer::Exception const & e)
  {
    LOG(LERROR, ("Error writing index file: ", e.what()));
  }

  return true;
}
