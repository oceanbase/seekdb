/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_PLAN_CACHE_
#define OCEANBASE_SQL_PLAN_CACHE_OB_PLAN_CACHE_

#include "lib/net/ob_addr.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/alloc/alloc_func.h"
#include "lib/task/ob_timer.h"
#include "sql/plan_cache/ob_plan_cache_util.h"
#include "sql/plan_cache/ob_id_manager_allocator.h"
#include "sql/plan_cache/ob_sql_parameterization.h"
#include "sql/plan_cache/ob_prepare_stmt_struct.h"
#include "sql/plan_cache/ob_pre_calc_expr_handler.h"
#include "sql/plan_cache/ob_lib_cache_key_creator.h"
#include "sql/plan_cache/ob_lib_cache_node_factory.h"
#include "sql/plan_cache/ob_lib_cache_object_manager.h"
namespace oceanbase
{
namespace observer
{
  class ObGVSql;
  class ObAllVirtualSqlPlan;
}
namespace pl
{
class ObPLFunction;
class ObPLPackage;
class ObGetPLKVEntryOp;
}  // namespace pl
using common::ObPsStmtId;
namespace sql
{
class ObPlanCacheValue;
class ObPlanCacheAtomicOp;
class ObPsPCVSetAtomicOp;
class ObSqlExecutorCtx;
struct ObSqlCtx;
class ObExecContext;
class ObPCVSet;
class ObILibCacheObject;
class ObPhysicalPlan;
class ObLibCacheAtomicOp;


struct ObKVEntryTraverseOp
{
  typedef common::hash::HashMapPair<ObILibCacheKey *, ObILibCacheNode *> LibCacheKVEntry;
  explicit ObKVEntryTraverseOp(LCKeyValueArray *key_val_list)
    : total_mem_used_(0),
      key_value_list_(key_val_list)
  {
  }

  virtual int check_entry_match(LibCacheKVEntry &entry, bool &is_match)
  {
    UNUSED(entry);
    int ret = OB_SUCCESS;
    is_match = true;
    return ret;
  }
  virtual int operator()(LibCacheKVEntry &entry)
  {
    int ret = common::OB_SUCCESS;
    bool is_match = false;
    if (OB_ISNULL(key_value_list_) || OB_ISNULL(entry.first) || OB_ISNULL(entry.second)) {
      ret = common::OB_INVALID_ARGUMENT;
      PL_CACHE_LOG(WARN, "invalid argument",
      K(key_value_list_), K(entry.first), K(entry.second), K(ret));
    } else if (OB_FAIL(check_entry_match(entry, is_match))) {
    } else if (is_match) {
      if (OB_FAIL(key_value_list_->push_back(ObLCKeyValue(entry.first, entry.second)))) {
      } else {
        entry.second->inc_ref_count();
        total_mem_used_ += entry.second->get_mem_size();
      }
    }
    return ret;
  }
  int64_t get_total_mem_used() const { return total_mem_used_; }
  LCKeyValueArray *get_key_value_list() { return key_value_list_; }

  int64_t total_mem_used_;
  LCKeyValueArray *key_value_list_;
};


struct ObDumpAllCacheObjOp
{
  explicit ObDumpAllCacheObjOp(common::ObIArray<AllocCacheObjInfo> *key_array,
                               int64_t safe_timestamp)
    : key_array_(key_array),
      safe_timestamp_(safe_timestamp)
  {
  }
  int operator()(common::hash::HashMapPair<uint64_t, ObILibCacheObject *> &entry)
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(key_array_)) {
      ret = OB_NOT_INIT;
      SQL_PC_LOG(WARN, "key array not inited", K(ret));
    } else if (OB_ISNULL(entry.second)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_PC_LOG(WARN, "unexpected null entry.second", K(ret));
    } else if (should_dump(entry.second)
              && OB_FAIL(key_array_->push_back(AllocCacheObjInfo(
                  entry.second->get_object_id(),
                  entry.second->get_logical_del_time(),
                  safe_timestamp_,
                  entry.second->get_ref_count(),
                  entry.second->get_allocator().used(),
                  entry.second->added_lc())))) {
      SQL_PC_LOG(WARN, "failed to push back element", K(ret));
    }
    return ret;
  }

protected:
  virtual bool should_dump(ObILibCacheObject *cache_obj) const
  {
    UNUSED(cache_obj);
    return true;
  }
protected:
  common::ObIArray<AllocCacheObjInfo> *key_array_;
  int64_t safe_timestamp_;
};

enum DumpType { DUMP_SQL, DUMP_PL, DUMP_ALL };
struct ObDumpAllCacheObjByTypeOp : ObDumpAllCacheObjOp
{
  explicit ObDumpAllCacheObjByTypeOp(common::ObIArray<AllocCacheObjInfo> *key_array,
                                     int64_t safe_timestamp,
                                     DumpType dump_type)
    : ObDumpAllCacheObjOp(key_array, safe_timestamp),
      dump_type_(dump_type)
  {
  }
  virtual bool should_dump(ObILibCacheObject *cache_obj) const
  {
    bool ret_bool = false;
    if (cache_obj->should_release(safe_timestamp_)) {
      ObLibCacheNameSpace ns = cache_obj->get_ns();
      if (DUMP_ALL == dump_type_) {
        ret_bool = true;
      } else if (DUMP_SQL == dump_type_) {
        ret_bool = (ObLibCacheNameSpace::NS_CRSR == ns);
      } else if (DUMP_PL == dump_type_) {
        ret_bool = (ObLibCacheNameSpace::NS_PRCR == ns
                  || ObLibCacheNameSpace::NS_SFC == ns
                  || ObLibCacheNameSpace::NS_PKG == ns
                  || ObLibCacheNameSpace::NS_ANON == ns);
      }
    }
    return ret_bool;
  }

  DumpType dump_type_;
};

struct ObDumpAllCacheObjByNsOp : ObDumpAllCacheObjOp
{
  explicit ObDumpAllCacheObjByNsOp(common::ObIArray<AllocCacheObjInfo> *key_array,
                                   int64_t safe_timestamp,
                                   ObLibCacheNameSpace ns)
    : ObDumpAllCacheObjOp(key_array, safe_timestamp),
      namespace_(ns)
  {
  }
  virtual bool should_dump(ObILibCacheObject *cache_obj) const
  {
    bool ret_bool = false;
    if (cache_obj->should_release(safe_timestamp_)) {
      ObLibCacheNameSpace ns = cache_obj->get_ns();
      ret_bool = (namespace_ == ns);
    }
    return ret_bool;
  }

  ObLibCacheNameSpace namespace_;
};

class ObPlanCacheEliminationTask : public common::ObTimerTask
{
public:
  ObPlanCacheEliminationTask() : plan_cache_(NULL),
                            run_task_counter_(0)
  {
  }
  void runTimerTask(void);
private:
  void run_plan_cache_task();
public:
  ObPlanCache* plan_cache_;
  int64_t run_task_counter_;
};

class ObPlanCache
{
friend class ObCacheObjectFactory;
friend class ObPlanCacheEliminationTask;
friend class observer::ObAllVirtualSqlPlan;
friend class observer::ObGVSql;

public:
  static const int64_t MAX_PLAN_SIZE = 20*1024*1024; //20M
  static const int64_t MAX_PLAN_CACHE_SIZE = 5*1024LL*1024LL*1024LL; // 5G
  static const int64_t EVICT_KEY_NUM = 8;
  static const int64_t MAX_RUNTIME_MEM = ((int64_t)(1) << 40); // 1T
  typedef common::hash::ObHashMap<ObILibCacheKey*, ObILibCacheNode*> CacheKeyNodeMap;
  typedef common::ObSEArray<uint64_t, 1024> PlanIdArray;

  ObPlanCache();
  virtual ~ObPlanCache();
  static int server_module_init(
      ObPlanCache *&plan_cache,
      query::ObIPlanCacheAccessService &access_service);
  static void server_module_stop(ObPlanCache * &plan_cache);
  int init(
      int64_t hash_bucket,
      query::ObIPlanCacheAccessService &access_service);
  bool is_inited() { return inited_; }
  query::ObIPlanCacheAccessService &access_service() const
  {
    OB_ASSERT_MSG(
        nullptr != access_service_,
        "plan-cache access service is not initialized");
    return *access_service_;
  }

  static int check_can_do_insert_opt(common::ObIAllocator &allocator,
                                     ObPlanCacheCtx &pc_ctx,
                                     ObFastParserResult &fp_result,
                                     bool &can_do_batch,
                                     int64_t &batch_count,
                                     ObString &first_truncated_sql,
                                     bool &is_insert_values);
  static int rebuild_raw_params(common::ObIAllocator &allocator,
                                ObPlanCacheCtx &pc_ctx,
                                ObFastParserResult &fp_result,
                                int64_t row_count);

  static int restore_param_to_truncated_sql(ObPlanCacheCtx &pc_ctx);

  static bool can_do_insert_batch_opt(ObPlanCacheCtx &pc_ctx);

  /**
   * Add new plan to PlanCache
   */
  int add_plan(ObPhysicalPlan *plan, ObPlanCacheCtx &pc_ctx);

  /**
   * Add new ps plan to PlanCache
   */
  template<class T>
  int add_ps_plan(T *plan,
                  ObPlanCacheCtx &pc_ctx);

  // cache object access functions
  /* Query the execution plan that meets the requirements from the plan cache based on ObPlanCacheKey and parameters */
  int get_plan(common::ObIAllocator &allocator, ObPlanCacheCtx &pc_ctx, ObCacheObjGuard& guard);
  /* Query the execution plan that meets the requirements from the plan cache based on ObPlanCacheKey and parameters */
  int get_ps_plan(ObCacheObjGuard& guard, const ObPsStmtId stmt_id, ObPlanCacheCtx &pc_ctx);
  int ref_cache_obj(const ObCacheObjID obj_id, ObCacheObjGuard& guard);
  int ref_plan(const ObCacheObjID obj_id, ObCacheObjGuard& guard);
  int add_cache_obj(ObILibCacheCtx &ctx, ObILibCacheKey *key, ObILibCacheObject *cache_obj);
  int get_cache_obj(ObILibCacheCtx &ctx, ObILibCacheKey *key, ObCacheObjGuard &guard);
  int evict_plan(uint64_t table_id);

  /**
   * memory related
   *    high water mark
   *    low water mark
   *    memory used
   */
  // Background thread will check memory-related settings every 30s, if updated it will change, therefore atomic operation is needed
  int set_mem_conf(const ObPCMemPctConf &conf);
  int update_memory_conf();
  int64_t get_mem_limit() const
  {
    int64_t runtime_mem = get_runtime_memory();
    int64_t mem_limit = -1;
    if (OB_UNLIKELY(0 >= runtime_mem || runtime_mem >= MAX_RUNTIME_MEM)) {
      mem_limit = MAX_RUNTIME_MEM * 0.05;
    } else {
      mem_limit = runtime_mem / 100 * get_mem_limit_pct();
    }
    return mem_limit;
  }
  int64_t get_mem_high() const { return get_mem_limit()/100 * get_mem_high_pct(); }
  int64_t get_mem_low() const { return get_mem_limit()/100 * get_mem_low_pct(); }

  int64_t get_mem_limit_pct() const { return ATOMIC_LOAD(&mem_limit_pct_); }
  int64_t get_mem_high_pct() const { return ATOMIC_LOAD(&mem_high_pct_); }
  int64_t get_mem_low_pct() const { return ATOMIC_LOAD(&mem_low_pct_); }
  void set_mem_limit_pct(int64_t pct) { ATOMIC_STORE(&mem_limit_pct_, pct); }
  void set_mem_high_pct(int64_t pct) { ATOMIC_STORE(&mem_high_pct_, pct); }
  void set_mem_low_pct(int64_t pct) { ATOMIC_STORE(&mem_low_pct_, pct); }

  int64_t get_managed_used() const { return ATOMIC_LOAD(&managed_used_); }
  void inc_managed_used(const int64_t mem_delta)
  {
    if (mem_delta > 0) {
      ATOMIC_FAA(&managed_used_, mem_delta);
    }
  }
  void dec_managed_used(const int64_t mem_delta)
  {
    if (mem_delta > 0) {
      int64_t old_value = 0;
      int64_t new_value = 0;
      do {
        old_value = ATOMIC_LOAD(&managed_used_);
        new_value = old_value > mem_delta ? old_value - mem_delta : 0;
      } while (!ATOMIC_BCAS(&managed_used_, old_value, new_value));
      if (OB_UNLIKELY(old_value < mem_delta)) {
        SQL_PC_LOG_RET(WARN, OB_ERR_UNEXPECTED,
            "plan cache managed memory accounting underflow",
            K(mem_delta), K(old_value));
      }
    }
  }
  void account_cache_object(ObILibCacheObject &cache_obj);
  void refresh_cache_node(ObILibCacheNode &cache_node);
  void release_cache_object(ObILibCacheObject &cache_obj);
  void release_cache_node(ObILibCacheNode &cache_node);

  int64_t get_mem_used() const { return get_managed_used(); }
  int64_t get_mem_hold() const;
  int64_t get_bucket_num() const { return bucket_num_; }

  // access count related
  void inc_access_cnt() { ATOMIC_INC(&pc_stat_.access_count_);}
  void inc_hit_and_access_cnt()
  {
    ATOMIC_INC(&pc_stat_.hit_count_);
    ATOMIC_INC(&pc_stat_.access_count_);
  }

  /*
   * cache evict
   */
  int cache_evict_all_plan();
  int cache_evict_all_obj();
  //evict plan, adjust mem between hwm and lwm
  int cache_evict();
  int cache_evict_by_glitch_node();
  int cache_evict_by_idle();
  int cache_evict_plan_by_sql_id(uint64_t db_id, common::ObString sql_id);
  int cache_evict_by_ns(ObLibCacheNameSpace ns);
  template<typename CallBack = ObKVEntryTraverseOp>
  int foreach_cache_evict(CallBack &cb);
  void destroy();
  common::ObAddr &get_host() { return host_; }
  void set_host(common::ObAddr &addr) { host_ = addr; }
  
  int64_t get_runtime_memory() const {
    return lib::get_memory_budget();
  }
  
  common::ObIAllocator *get_pc_allocator() { return &inner_allocator_; }
  common::ObIAllocator &get_pc_allocator_ref() { return inner_allocator_; }
  int64_t get_cache_obj_size() const { return co_mgr_.get_cache_obj_size(); }
  ObPlanCacheStat &get_plan_cache_stat() { return pc_stat_; }
  const ObPlanCacheStat &get_plan_cache_stat() const { return pc_stat_; }
  int remove_cache_obj_stat_entry(const ObCacheObjID cache_obj_id);
  int remove_cache_node(ObILibCacheKey *key);
  ObLCObjectManager &get_cache_obj_mgr() { return co_mgr_; }
  ObLCNodeFactory &get_cache_node_factory() { return cn_factory_; }
  int alloc_cache_obj(ObCacheObjGuard& guard, ObLibCacheNameSpace ns);
  void free_cache_obj(ObILibCacheObject *&cache_obj);
  int destroy_cache_obj(const bool is_leaked, const uint64_t object_id);
  static int construct_fast_parser_result(common::ObIAllocator &allocator,
                                          ObPlanCacheCtx &pc_ctx,
                                          const common::ObString &raw_sql,
                                          ObFastParserResult &fp_result);
  static int construct_multi_stmt_fast_parser_result(common::ObIAllocator &allocator,
                                                     ObPlanCacheCtx &pc_ctx);
  int dump_deleted_objs_by_ns(ObIArray<AllocCacheObjInfo> &deleted_objs,
                              const int64_t safe_timestamp,
                              const ObLibCacheNameSpace ns);
  template<DumpType dump_type>
  int dump_deleted_objs(common::ObIArray<AllocCacheObjInfo> &deleted_objs,
                        const int64_t safe_timestamp) const;
  template<typename _callback>
  int foreach_cache_obj(_callback &callback) const;
  template<typename _callback>
  int foreach_alloc_cache_obj(_callback &callback) const;

  common::ObMemAttr get_mem_attr() {
    common::ObMemAttr attr;
    attr.label_ = ObNewModIds::OB_SQL_PLAN_CACHE;
    
    attr.ctx_id_ = ObCtxIds::PLAN_CACHE_CTX_ID;
    return attr;
  }

  TO_STRING_KV(
               K_(mem_limit_pct),
               K_(mem_high_pct),
               K_(mem_low_pct));

public:
  int flush_plan_cache();
  int flush_plan_cache_by_sql_id(uint64_t db_id, common::ObString sql_id);
  template<typename GETPLKVEntryOp, typename EvictAttr>
  int flush_pl_cache_single_cache_obj(uint64_t db_id, EvictAttr &attr);
  int flush_lib_cache();
  int flush_lib_cache_by_ns(const ObLibCacheNameSpace ns);
  int flush_pl_cache();

protected:
  int ref_alloc_obj(const ObCacheObjID obj_id, ObCacheObjGuard& guard);
  int ref_alloc_plan(const ObCacheObjID obj_id, ObCacheObjGuard& guard);

private:
  DISALLOW_COPY_AND_ASSIGN(ObPlanCache);
  int add_plan_cache(ObILibCacheCtx &ctx,
                     ObILibCacheObject *cache_obj);
  int get_plan_cache(ObILibCacheCtx &ctx,
                     ObCacheObjGuard &guard);
  int get_value(ObILibCacheKey *key,
                ObILibCacheNode *&node,
                ObLibCacheAtomicOp &op);
  int add_cache_obj_stat(ObILibCacheCtx &ctx,
                         ObILibCacheObject *cache_obj);
  bool calc_evict_num(int64_t &plan_cache_evict_num);

  int batch_remove_cache_node(const LCKeyValueArray &to_evict);
  bool is_reach_memory_limit() { return get_managed_used() > get_mem_limit(); }
  int construct_plan_cache_key(ObPlanCacheCtx &plan_ctx, ObLibCacheNameSpace ns);
  static int construct_plan_cache_key(ObSQLSessionInfo &session,
                                      ObLibCacheNameSpace ns,
                                      ObPlanCacheKey &pc_key);
  int add_stat_for_cache_obj(ObILibCacheCtx &ctx, ObILibCacheObject *cache_obj);
  int create_node_and_add_cache_obj(ObILibCacheKey *key,
                                    ObILibCacheCtx &ctx,
                                    ObILibCacheObject *cache_obj,
                                    ObILibCacheNode *&node);
  int check_after_get_plan(int tmp_ret, ObILibCacheCtx &ctx, ObILibCacheObject *cache_obj);
  int get_normalized_pattern_digest(const ObPlanCacheCtx &pc_ctx, uint64_t &pattern_digest);
private:
private:
  const static int64_t SLICE_SIZE = 1024; //1k
private:
  bool inited_;
  query::ObIPlanCacheAccessService *access_service_;
  
  int64_t mem_limit_pct_;
  int64_t mem_high_pct_;                     // high water mark percentage
  int64_t mem_low_pct_;                      // low water mark percentage
  int64_t managed_used_;
  int64_t bucket_num_;
  lib::MemoryContext root_context_;
  common::ObMalloc inner_allocator_;
  common::ObAddr host_;
  ObPlanCacheStat pc_stat_;
  // mark this Plan Cache whether is destroying.
  volatile int64_t destroy_;
  ObLCObjectManager co_mgr_;
  ObLCNodeFactory cn_factory_;
  CacheKeyNodeMap cache_key_node_map_;
  ObPlanCacheEliminationTask evict_task_;
  common::ObTimer evict_timer_;
  int64_t idle_scan_cursor_;
  bool idle_evict_done_round_;
  static const int64_t IDLE_SCAN_MAX_NODES = 1000;
  static const int64_t IDLE_SCAN_MAX_BUCKETS = 5000;
  static const int64_t IDLE_EVICT_THRESHOLD_US = 30L * 1000L * 1000L; // 30s
};

template<typename _callback>
int ObPlanCache::foreach_cache_obj(_callback &callback) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(co_mgr_.foreach_cache_obj(callback))) {
  }
  return ret;
}

template<typename _callback>
int ObPlanCache::foreach_alloc_cache_obj(_callback &callback) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(co_mgr_.foreach_alloc_cache_obj(callback))) {
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_PLAN_CACHE_H */
