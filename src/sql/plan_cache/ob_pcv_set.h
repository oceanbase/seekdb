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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_PCV_SET_
#define OCEANBASE_SQL_PLAN_CACHE_OB_PCV_SET_

#include "lib/list/ob_dlist.h"
#include "lib/string/ob_string.h"
#include "lib/stat/ob_latch_define.h"
#include "sql/session/ob_basic_session_info.h"
#include "sql/plan_cache/ob_plan_cache_value.h"
#include "sql/plan_cache/ob_plan_cache.h"
#include "sql/plan_cache/ob_plan_cache_util.h"
#include "observer/omt/ob_th_worker.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObString;
}

namespace sql
{
class ObPhysicalPlan;
class ObPlanCache;
class ObPlanCacheValue;
class ObPlanCacheCtx;

class ObPCVSet : public ObILibCacheNode
{

struct PCColStruct
{
  common::ObSEArray<int64_t, 16> param_idxs_;
  bool is_last_;

  PCColStruct()
    : param_idxs_(),
      is_last_(false) {}

  void reset()
  {
    param_idxs_.reset();
    is_last_ = false;
  }
  TO_STRING_KV(K_(param_idxs),
               K_(is_last))
};

public:
  ObPCVSet(ObPlanCache *lib_cache, lib::MemoryContext &mem_context)
    : ObILibCacheNode(lib_cache, mem_context),
      is_inited_(false),
      pc_key_(),
      pc_alloc_(NULL),
      sql_(),
      normal_parse_const_cnt_(0),
      min_cluster_version_(0),
      plan_num_(0),
      need_check_gen_tbl_col_(false),
      expired_time_(0)
  {
  }
  virtual ~ObPCVSet()
  {
    destroy();
  };
  //init pcv set
  virtual int init(ObILibCacheCtx &ctx, const ObILibCacheObject *cache_obj);
  virtual int inner_get_cache_obj(ObILibCacheCtx &ctx,
                                  ObILibCacheKey *key,
                                  ObILibCacheObject *&cache_obj);
  virtual int inner_add_cache_obj(ObILibCacheCtx &ctx,
                                  ObILibCacheKey *key,
                                  ObILibCacheObject *cache_obj);
  void destroy();
  common::ObIArray<common::ObString> &get_sql_id() { return sql_ids_; }
  int push_sql_id(common::ObString sql_id) { return sql_ids_.push_back(sql_id); }
  ObPlanCache *get_plan_cache() const { return lib_cache_; }
  common::ObIAllocator *get_pc_allocator() const { return pc_alloc_; }
  void set_plan_cache_key(ObPlanCacheKey &key) { pc_key_ = key; }
  // @shaoge
  // plan cache, the same pcv_set object may exist with three records in the map at the same time, these three records have keys of
  // old_stmt_id, new_stmt_id and sql, the key returned by the following interface is old_stmt_id
  ObPlanCacheKey &get_plan_cache_key() { return pc_key_; }
  const ObString &get_sql() { return sql_; }
  int deep_copy_sql(const common::ObString &sql);
  int check_contains_table(uint64_t db_id, common::ObString tab_name, bool &contains);
  bool set_expired_time();

  TO_STRING_KV(K_(is_inited));

private:
  static const int64_t MAX_PCV_SET_PLAN_NUM = 200;
  int create_pcv_and_add_plan(ObPlanCacheObject *cache_obj,
                              ObPlanCacheCtx &pc_ctx,
                              const common::ObIArray<PCVSchemaObj> &schema_array,
                              ObPlanCacheValue *&value);
  int64_t get_plan_num() const { return plan_num_; }
  int create_new_pcv(ObPlanCacheValue *&new_pcv);
  void free_pcv(ObPlanCacheValue *pcv);
  // If sql contains a subquery, and the projection columns of the subquery are parameterized, due to the constraint of having the same column names in the subquery, plan cache needs to perform constraint checking when matching
  // For example select * (select 1, 2, 3 from dual), parameterized as select * (select ?, ?, ? from dual), plan cache cached this plan
  // If a sql select * (select 1, 1, 3 from dual) comes in, it hits the plan, but the subquery has ambiguous columns
  // plan cache should reject hitting the plan, and let sql go through hard parsing, throw duplicate column name error
  int set_raw_param_info_if_needed(ObPlanCacheObject *cache_obj);
  int check_raw_param_for_dup_col(ObPlanCacheCtx &pc_ctx, bool &contain_dup_col);
private:
  bool is_inited_;
  ObPlanCacheKey pc_key_; //used for manager key memory
  common::ObIAllocator *pc_alloc_;
  common::ObString sql_;  // when adding a kv pair with sql as the key to the plan cache, this member is needed
  common::ObDList<ObPlanCacheValue> pcv_list_;
  // Number of constants that can be recognized by normal parser, used to verify if the number of constants recognized by faster parse is consistent with that recognized by normal parser.
  int64_t normal_parse_const_cnt_;
  int64_t min_cluster_version_;
  // Record how many plans are hanging under this pcv_set, with a limit of MAX_PCV_SET_PLAN_NUM
  int64_t plan_num_;
  common::ObSEArray<common::ObString, 4> sql_ids_;
  bool need_check_gen_tbl_col_;
  common::ObFixedArray<PCColStruct, common::ObIAllocator> col_field_arr_;
  int64_t expired_time_;
};

}
}

#endif
