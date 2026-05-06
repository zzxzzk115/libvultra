#pragma once

#include "vultra/function/asset/asset_record.hpp"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace vultra
{
    template<class TCpu, class TGpu, uint32_t ShardCount = 64>
    class AssetCache
    {
    public:
        using Record = AssetRecord<TCpu, TGpu>;

        Record* findOrCreate(const CoreUUID& uuid)
        {
            auto&            shard = m_Shards[std::hash<CoreUUID> {}(uuid) % ShardCount];
            std::scoped_lock lock(shard.mtx);

            auto it = shard.map.find(uuid);
            if (it != shard.map.end())
                return it->second.get();

            auto rec  = std::make_unique<Record>();
            rec->uuid = uuid;

            Record* ptr = rec.get();
            shard.map.emplace(uuid, std::move(rec));
            return ptr;
        }

        template<class Fn>
        void forEachRecord(Fn&& fn)
        {
            for (auto& shard : m_Shards)
            {
                std::scoped_lock lock(shard.mtx);
                for (auto& [_, rec] : shard.map)
                    fn(*rec);
            }
        }

        template<class Fn>
        void forEachRecord(Fn&& fn) const
        {
            for (const auto& shard : m_Shards)
            {
                std::scoped_lock lock(shard.mtx);
                for (const auto& [_, rec] : shard.map)
                    fn(*rec);
            }
        }

        void clear()
        {
            for (auto& shard : m_Shards)
            {
                std::scoped_lock lock(shard.mtx);
                shard.map.clear();
            }
        }

    private:
        struct Shard
        {
            mutable std::mutex                                     mtx;
            std::unordered_map<CoreUUID, std::unique_ptr<Record>> map;
        };

        Shard m_Shards[ShardCount];
    };
} // namespace vultra
