#pragma once

#include "tile_info.hpp"
#include "memory_feature_index.hpp"
#include "engine_context.hpp"

#include "../base/thread.hpp"
#include "../base/object_tracker.hpp"

#include "../map/feature_vec_model.hpp"

namespace df
{
  class ReadMWMTask : public threads::IRoutine
  {
  public:
    struct LessByTileKey
    {
      bool operator()( const ReadMWMTask * l, const ReadMWMTask * r ) const
      {
        return l->GetTileInfo().m_key < r->GetTileInfo().m_key;
      }
    };

    ReadMWMTask(TileKey const & tileKey,
                model::FeaturesFetcher const & model,
                MemoryFeatureIndex & index,
                EngineContext & context);

    ~ReadMWMTask();

    virtual void Do();

    df::TileInfo const & GetTileInfo() const;

    void PrepareToRestart();
    void Finish();
    bool IsFinished();

  private:
    void ReadTileIndex();
    void ReadGeometry(const FeatureID & id);

  private:
    TileInfo m_tileInfo;
    model::FeaturesFetcher const & m_model;
    MemoryFeatureIndex & m_index;
    EngineContext & m_context;
    bool m_isFinished;

  #ifdef DEBUG
    dbg::ObjectTracker m_objTracker;
  #endif
  };
}
