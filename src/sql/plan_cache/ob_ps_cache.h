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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_PS_CACHE_
#define OCEANBASE_SQL_PLAN_CACHE_OB_PS_CACHE_

#include "lib/hash/ob_hashmap.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/allocator/page_arena.h"
#include "lib/task/ob_timer.h"
#include "sql/plan_cache/ob_prepare_stmt_struct.h"

namespace oceanbase
{

namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
}
}
using common::ObPsStmtId;
namespace sql
{

class ObPsPrepareStatusGuard final
{
public:
  explicit ObPsPrepareStatusGuard(ObSQLSessionInfo &session_info)
    : session_info_(session_info)
  {
    old_value_ = session_info_.is_varparams_sql_prepare();
  }
  ~ObPsPrepareStatusGuard()
  {
    session_info_.set_is_varparams_sql_prepare(old_value_);
  }
  void is_varparams_sql_prepare(bool with_template)
  {
    if (with_template) {
      session_info_.set_is_varparams_sql_prepare(true);
    }
  }
private:
  ObSQLSessionInfo &session_info_;
  bool old_value_;
};
class ObPsCacheEliminationTask : public common::ObTimerTask
{
public:
  ObPsCacheEliminationTask() : ps_cache_(NULL)
  {
  }
  void runTimerTask(void);
public:
  ObPsCache* ps_cache_;
};

class ObPsCache
{
public:
  //sql -> stmt_id
  typedef common::hash::ObHashMap<ObPsSqlKey, ObPsStmtItem *, common::hash::SpinReadWriteDefendMode> PsStmtIdMap;
  //stmt_id -> plan
  typedef common::hash::ObHashMap<ObPsStmtId, ObPsStmtInfo *, common::hash::SpinReadWriteDefendMode> PsStmtInfoMap;
  typedef common::hash::ObHashMap<ObPsStmtId, ObPsSessionInfo *, common::hash::SpinReadWriteDefendMode> PsSessionInfoMap;

  ObPsCache();
  virtual ~ObPsCache();
  static int server_module_init(ObPsCache* &ps_cache);
  static void server_module_stop(ObPsCache * &ps_cache);
  int init(const int64_t hash_bucket);
  bool is_inited() const { return inited_; }
  int set_mem_conf(const ObPCMemPctConf &conf);
  int update_memory_conf();
  void destroy();

public:
  // always make sure stmt_id is inner_stmt_id!!!
  
  int get_stmt_info_guard(const ObPsStmtId ps_stmt_id, ObPsStmtInfoGuard &guard);
  int ref_stmt_item(const ObPsSqlKey &ps_sql_key, ObPsStmtItem *&stmt_item);
  int ref_stmt_info(const ObPsStmtId stmt_id, ObPsStmtInfo *&ps_stmt_info);
  int deref_stmt_info(const ObPsStmtId stmt_id);
  int inner_ref_stmt_item(const ObPsSqlKey &ps_sql_key, ObPsStmtItem *&ps_stmt_item);
  int deref_stmt_item(const ObPsSqlKey &ps_sql_key);
  int deref_ps_stmt(const ObPsStmtId stmt_id);
  int get_or_add_stmt_item(const ObPsSqlKey &ps_key,
                           const bool is_contain_tmp_tbl,
                           ObPsStmtItem *&ps_item_value);
  int get_or_add_stmt_info(const PsCacheInfoCtx &info_ctx,
                           const ObResultSet &result,
                           ObSchemaGetterGuard &schema_guard,
                           ObPsStmtItem *ps_item,
                           ObPsStmtInfo *&ref_ps_info);

  int cache_evict();
  int cache_evict_all_ps();

  inline void inc_access_count() { ATOMIC_INC(&access_count_); }
  inline uint64_t get_access_count() { return ATOMIC_LOAD(&access_count_); }
  inline uint64_t get_hit_count() { return ATOMIC_LOAD(&hit_count_); }
  inline void inc_access_and_hit_count()
  {
    ATOMIC_INC(&hit_count_);
    ATOMIC_INC(&access_count_);
  }

  int mem_total(int64_t &mem_total) const;
  int64_t get_managed_used() const { return ATOMIC_LOAD(&managed_used_); }
  void get_memory_info(int64_t &hold, int64_t &used, int64_t &mem_limit) const;
  void release_managed_memory(const int64_t size);

  inline int64_t get_stmt_id_map_size() { return stmt_id_map_.size(); }
  inline int64_t get_stmt_info_map_size() { return stmt_info_map_.size(); }

  int get_all_stmt_id(common::ObIArray<ObPsStmtId> *id_array);
  int check_schema_version(ObSchemaGetterGuard &schema_guard,
                           ObPsStmtInfo &stmt_info,
                           bool &is_expired);
  int erase_stmt_item(ObPsStmtId stmt_id, const ObPsSqlKey &ps_key);

private:
  int destroy_cached_ps(const ObPsStmtId inner_stmt_id);
  int inner_cache_evict(bool is_evict_all);
  int fill_ps_stmt_info(const ObResultSet &result,
                        int64_t param_cnt,
                        ObPsStmtInfo &ps_stmt_info) const;
  int add_stmt_info(const ObPsStmtItem &ps_item,
                    const ObPsStmtInfo &ps_info,
                    ObPsStmtInfo *&ref_ps_info);
  
  inline ObPsStmtId gen_new_ps_stmt_id() { return __sync_add_and_fetch(&next_ps_stmt_id_, 1); }

  int64_t get_mem_limit() const
  {
    const double PS_EVICT_PERCENT_ON_PC = 0.5;
    const int64_t MAX_RUNTIME_MEM = ((int64_t)(1) << 40); // 1T
    int64_t runtime_mem = lib::get_memory_budget();
    int64_t mem_limit = -1;
    if (OB_UNLIKELY(0 >= runtime_mem || runtime_mem >= MAX_RUNTIME_MEM)) {
      mem_limit = MAX_RUNTIME_MEM * PS_EVICT_PERCENT_ON_PC;
    }
    mem_limit = runtime_mem / 200 * get_mem_limit_pct();
    return mem_limit;
  }

  int64_t get_mem_high() const { return get_mem_limit()/100 * get_mem_high_pct(); }
  int64_t get_mem_low() const { return get_mem_limit()/100 * get_mem_low_pct(); }

  inline int64_t get_mem_limit_pct() const { return ATOMIC_LOAD(&mem_limit_pct_); }
  inline int64_t get_mem_high_pct() const { return ATOMIC_LOAD(&mem_high_pct_); }
  inline int64_t get_mem_low_pct() const { return ATOMIC_LOAD(&mem_low_pct_); }

  inline void set_mem_limit_pct(int64_t pct) { ATOMIC_STORE(&mem_limit_pct_, pct); }
  inline void set_mem_high_pct(int64_t pct) { ATOMIC_STORE(&mem_high_pct_, pct); }
  inline void set_mem_low_pct(int64_t pct) { ATOMIC_STORE(&mem_low_pct_, pct); }

private:
  static const int64_t SLICE_SIZE = 1024;

  ObPsStmtId next_ps_stmt_id_;
  bool inited_;
  
  common::ObAddr host_;
  PsStmtIdMap stmt_id_map_;
  PsStmtInfoMap stmt_info_map_;

  int64_t mem_limit_pct_;
  int64_t mem_high_pct_;                     // high water mark percentage
  int64_t mem_low_pct_;                      // low water mark percentage

  uint64_t hit_count_;
  uint64_t access_count_;

  lib::ObMutex mutex_;
  lib::MemoryContext mem_context_;
  common::ObIAllocator *inner_allocator_;
  int64_t managed_used_;
  int64_t bucket_charge_;
  ObPsCacheEliminationTask evict_task_;
  common::ObTimer evict_timer_;

  static int64_t stmt_id_entry_charge();
  static int64_t stmt_info_entry_charge();
  static int64_t stmt_id_bucket_charge(const int64_t bucket_count);
  static int64_t stmt_info_bucket_charge(const int64_t bucket_count);
  void add_managed_memory(const int64_t size);
  void account_stmt_item(ObPsStmtItem &item, const int64_t size);
  void account_stmt_info(ObPsStmtInfo &info, const int64_t size);
  void rollback_stmt_item(ObPsStmtItem &item);
  void rollback_stmt_info(ObPsStmtInfo &info);
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_SQL_PLAN_CACHE_OB_PS_CACHE_
