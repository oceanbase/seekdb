// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real cache map, PL nodes, collector, erasure and reference counters. Only
// cache-access tracking and injected error sources are fixtures; no SQL server.
#ifndef SEEKDB_TEST_PLAN_CACHE_EVICTION_FIXTURE_H_
#define SEEKDB_TEST_PLAN_CACHE_EVICTION_FIXTURE_H_
#include "sql/pl/pl_cache/ob_pl_cache_mgr.h"
#include "query/plan_cache/ob_plan_cache_access_service.h"
#include "share/schema/routine_catalog_transaction.h"
#include "data_plane/transaction/ob_tx_seq.h"

namespace oceanbase { namespace sql {
class PlanCacheEvictionTestAccess
{
public:
  static void run()
  {
    using namespace common;
    using namespace pl;
    struct Access final : query::ObIPlanCacheAccessService {
      int depth = 0, checks = 0;
      void enter_access() override { CHECK(depth++ == 0); }
      void leave_access() override { CHECK(--depth == 0); }
      void check_current_thread() override { CHECK(depth == 1); ++checks; }
      int get_global_safe_timestamp(int64_t &) const override { CHECK(false); return OB_ERR_UNEXPECTED; }
    } access;
    // Bind only the real map and access service. No fake initialized server,
    // cache object manager, periodic timer or global schema state is installed.
    auto cache = std::make_unique<ObPlanCache>();
    // The runner deliberately reuses main.cpp's flags (without SQL's plugin
    // definition) and links the plugin-enabled cache constructor. These member
    // accesses also exercise the shared header's cross-target layout contract.
    CHECK(!cache->inited_ && cache->access_service_ == nullptr);
    CHECK(!cache->plugin_invalidations_ && !cache->cache_key_node_map_.created());
    cache->access_service_ = &access;
    CHECK(cache->cache_key_node_map_.create(17, "PluginEvictTest", "PluginEvictTest") == OB_SUCCESS);
    lib::MemoryContext memory;
    lib::ContextParam param;
    param.set_mem_attr(ObModIds::OB_PL_TEMP, ObCtxIds::DEFAULT_CTX_ID);
    CHECK(CURRENT_CONTEXT->CREATE_CONTEXT(memory, param) == OB_SUCCESS);
    {
      query::ObPlanCacheAccessGuard region(access);
      ObPLObjectKey keys[3];
      const char *variables[] = {"a", "b", "c"};
      std::unique_ptr<ObPLObjectSet> nodes[3];
      for (int i = 0; i < 3; ++i) {
        keys[i].namespace_ = NS_SFC; keys[i].db_id_ = 100; keys[i].key_id_ = 900;
        keys[i].sys_vars_str_ = ObString::make_string(variables[i]);
        nodes[i] = std::make_unique<ObPLObjectSet>(cache.get(), memory);
        // A fixture-owned external reference prevents factory destruction, so
        // successful eviction can be observed before explicit fixture teardown.
        CHECK(nodes[i]->inc_ref_count() == 1);
        CHECK(nodes[i]->inc_ref_count() == 2); // Real map ownership.
        CHECK(cache->cache_key_node_map_.set_refactored(&keys[i], nodes[i].get()) == OB_SUCCESS);
      }
      struct FaultingCollector final : ObGetPLKVEntryBySchemaIdOp {
        int calls = 0, fail_at = 0;
        ObPLObjectKey *fault_key = nullptr;
        explicit FaultingCollector(LCKeyValueArray &entries)
            : ObGetPLKVEntryBySchemaIdOp(100, 900, &entries) {}
        int check_entry_match(LibCacheKVEntry &entry, bool &match) override {
          if (++calls == fail_at) return OB_TIMEOUT;
          return ObGetPLKVEntryBySchemaIdOp::check_entry_match(entry, match);
        }
        int operator()(LibCacheKVEntry &entry) override {
          // A synthetic null map value supplies remove_cache_node's real error
          // path. It owns no reference and is not traversed as a valid PL node.
          if (entry.second == nullptr) return OB_SUCCESS;
          int ret = ObGetPLKVEntryBySchemaIdOp::operator()(entry);
          if (ret == OB_SUCCESS && fault_key && key_value_list_->count() == 1) {
            ret = key_value_list_->push_back(ObLCKeyValue(fault_key, nullptr));
          }
          return ret;
        }
      };
      for (int attempt = 0; attempt < 16; ++attempt) {
        LCKeyValueArray entries;
        FaultingCollector collector(entries);
        collector.fail_at = attempt % 3 + 1;
        CHECK(cache->foreach_cache_evict<ObGetPLKVEntryBySchemaIdOp>(collector) == OB_TIMEOUT);
        CHECK(entries.count() == collector.fail_at - 1);
        CHECK(cache->cache_key_node_map_.size() == 3);
        for (auto &node : nodes) CHECK(node->get_ref_count() == 2);
      }
      ObGetPLKVEntryBySchemaIdOp missing_list(100, 900, nullptr);
      CHECK(cache->foreach_cache_evict(missing_list) == OB_INVALID_ARGUMENT);
      for (auto &node : nodes) CHECK(node->get_ref_count() == 2);
      // Real partial removal: one valid node is removed, then the null map
      // entry returns OB_ERR_UNEXPECTED. Every collected pin must still drop.
      ObPLObjectKey bad(100, 901); bad.namespace_ = NS_SFC;
      CHECK(cache->cache_key_node_map_.set_refactored(&bad, nullptr) == OB_SUCCESS);
      LCKeyValueArray entries;
      FaultingCollector collector(entries); collector.fault_key = &bad;
      CHECK(cache->foreach_cache_evict<ObGetPLKVEntryBySchemaIdOp>(collector) == OB_ERR_UNEXPECTED);
      CHECK(entries.count() == 4 && entries.at(1).node_ == nullptr);
      CHECK(cache->cache_key_node_map_.size() == 2);
      for (auto &node : nodes) {
        CHECK(node->get_ref_count() == (node.get() == entries.at(0).node_ ? 1 : 2));
      }
      // The same backend used by the Rust queue can retry the surviving nodes.
      share::schema::RoutineInvalidationQueue queue(1, 1);
      share::schema::RoutineCatalogTransaction journal(771);
      const transaction::ObTxSEQ barrier(10, 0);
      CHECK(journal.admit_ddl(771, barrier, 17) == OB_SUCCESS);
      CHECK(journal.record_schema_version(771, barrier, 500) == OB_SUCCESS);
      CHECK(journal.record_invalidation(771, barrier, 100, 900) == OB_SUCCESS);
      uint64_t version = 0, operations = 0;
      CHECK(journal.begin_prepare(771, version, operations) == OB_SUCCESS);
      CHECK(queue.reserve(journal, 771) == OB_SUCCESS);
      CHECK(journal.record_end_sign(771, 501) == OB_SUCCESS);
      CHECK(journal.complete_prepare(771, 501, OB_SUCCESS) == OB_SUCCESS);
      CHECK(journal.finish(771, true) == OB_SUCCESS);
      struct Evictor final : share::schema::IRoutineCacheEvictor {
        ObPlanCache &cache;
        int calls = 0;
        explicit Evictor(ObPlanCache &cache) : cache(cache) {}
        int check_schema_version(int64_t version) override { CHECK(version == 501); return OB_SUCCESS; }
        int evict(uint64_t db, uint64_t routine) override {
          CHECK(db == 100 && routine == 900);
          if (++calls == 1) {
            LCKeyValueArray entries;
            FaultingCollector collector(entries); collector.fail_at = 2;
            return cache.foreach_cache_evict<ObGetPLKVEntryBySchemaIdOp>(collector);
          }
          return ObPLCacheMgr::cache_evict_pl_cache_single<ObGetPLKVEntryBySchemaIdOp>(&cache, db, routine);
        }
      } evictor(*cache);
      uint32_t processed = 99;
      CHECK(queue.process(evictor, 1, processed) == OB_TIMEOUT && processed == 0 && evictor.calls == 1);
      CHECK(cache->cache_key_node_map_.size() == 2);
      for (auto &node : nodes) {
        CHECK(node->get_ref_count() == (node.get() == entries.at(0).node_ ? 1 : 2));
      }
      CHECK(queue.process(evictor, 1, processed) == OB_SUCCESS && processed == 1 && evictor.calls == 2);
      CHECK(queue.process(evictor, 1, processed) == OB_SUCCESS && processed == 0 && evictor.calls == 2);
      uint64_t routine = 900;
      CHECK(cache->cache_key_node_map_.size() == 0);
      for (auto &node : nodes) CHECK(node->get_ref_count() == 1);
      CHECK(ObPLCacheMgr::cache_evict_pl_cache_single<ObGetPLKVEntryBySchemaIdOp>(cache.get(), 100, routine) == OB_SUCCESS);
      for (auto &node : nodes) CHECK(node->get_ref_count() == 1);
      CHECK(cache->foreach_cache_evict(missing_list) == OB_ERR_UNEXPECTED);
    }
    CHECK(access.depth == 0 && access.checks == 22);
    cache->cache_key_node_map_.destroy();
    cache->access_service_ = nullptr;
    DESTROY_CONTEXT(memory);
  }
};
} }
#endif
