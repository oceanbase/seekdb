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

#ifndef OCEANBASE_OBSERVER_OB_TABLE_OBJECT_POOL_H_
#define OCEANBASE_OBSERVER_OB_TABLE_OBJECT_POOL_H_
#include "ob_table_sess_pool.h"
#include "ob_table_object_pool_common.h"
#include "ob_table_system_variable.h"
#include "observer/omt/ob_tenant.h"

namespace oceanbase
{
namespace table
{

class ObTableObjectPoolMgr final
{
public:
  ObTableObjectPoolMgr()
      : is_inited_(false),
        allocator_("TbObjPoolMgr", OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID()),
        sess_pool_(nullptr),
        ls_op_pool_(MTL_ID(), "LsOpPool", static_cast<omt::ObTenant *>(MTL_CTX())->max_worker_cnt(), REQUESE_RESULT_RETIRE_TIME),
        ls_res_pool_(MTL_ID(), "LsOpPool", static_cast<omt::ObTenant *>(MTL_CTX())->max_worker_cnt(), REQUESE_RESULT_RETIRE_TIME)
  {
    ls_op_pool_.set_obj_label("LsOpObj");
    ls_res_pool_.set_obj_label("LsResObj");
  }
  virtual ~ObTableObjectPoolMgr() { destroy(); }
  TO_STRING_KV(K_(is_inited),
               KPC_(sess_pool),
               K_(ls_op_pool),
               K_(ls_res_pool)
               );
public:
  class ObTableSessEliminationTask : public common::ObTimerTask
  {
  public:
    ObTableSessEliminationTask()
        : is_inited_(false),
          obj_pool_mgr_(nullptr)
    {
    }
    TO_STRING_KV(K_(is_inited), KPC_(obj_pool_mgr));
    void runTimerTask(void);
  private:
    // Recycle the obsolete session
    int run_recycle_retired_sess_task();
    // Evict sessions that have not been used for a long time
    int run_retire_sess_task();
  public:
    bool is_inited_;
    ObTableObjectPoolMgr *obj_pool_mgr_;
  };
  class ObTableSessSysVarUpdateTask : public common::ObTimerTask
  {
  public:
    ObTableSessSysVarUpdateTask()
        : is_inited_(false),
          obj_pool_mgr_(nullptr)
    {
    }
    TO_STRING_KV(K_(is_inited), KPC_(obj_pool_mgr));
    void runTimerTask(void);
  private:
    int run_update_sys_var_task();
  public:
    bool is_inited_;
    ObTableObjectPoolMgr *obj_pool_mgr_;
  };
  class ObTableReqResEliminationTask : public common::ObTimerTask
  {
  public:
    ObTableReqResEliminationTask()
        : is_inited_(false),
          obj_pool_mgr_(nullptr)
    {
    }
    TO_STRING_KV(K_(is_inited), KPC_(obj_pool_mgr));
    void runTimerTask(void);
  private:
    int run_recycle_retired_request_result_task();
  public:
    bool is_inited_;
    ObTableObjectPoolMgr *obj_pool_mgr_;
  };
public:
  static int mtl_init(ObTableObjectPoolMgr *&mgr);
  int start();
  void stop();
  void wait();
  void destroy();
  int init();
  int get_sess_info(ObTableApiCredential &credential, ObTableApiSessGuard &guard);
  int update_sess(ObTableApiCredential &credential);
  int update_sys_vars(bool only_update_dynamic_vars)
  {
    return sys_vars_.update_sys_vars(only_update_dynamic_vars);
  }
public:
  OB_INLINE int64_t get_binlog_row_image() const
  {
    return sys_vars_.dynamic_vars_.get_binlog_row_image();
  }
  OB_INLINE ObKvModeType get_kv_mode() const
  {
    return sys_vars_.static_vars_.get_kv_mode();
  }
  OB_INLINE int64_t get_kv_group_commit_batch_size() const
  {
    return sys_vars_.dynamic_vars_.get_kv_group_commit_batch_size();
  }
  OB_INLINE ObTableGroupRwMode get_group_rw_mode() const
  {
    return sys_vars_.dynamic_vars_.get_group_rw_mode();
  }
  OB_INLINE int64_t get_query_record_size_limit() const
  {
    return sys_vars_.dynamic_vars_.get_query_record_size_limit();
  }
  OB_INLINE bool is_enable_query_response_time_stats() const
  {
    return sys_vars_.dynamic_vars_.is_enable_query_response_time_stats();
  }
  OB_INLINE bool is_support_distributed_execute() const
  {
    return sys_vars_.dynamic_vars_.is_support_distributed_execute();
  }
  OB_INLINE int alloc_ls_op(ObTableLSOp *&op) { return ls_op_pool_.get_object(op); }
  OB_INLINE void free_ls_op(ObTableLSOp *op) { ls_op_pool_.free_object(op); }
  OB_INLINE int alloc_res(ObTableLSOpResult *&res) { return ls_res_pool_.get_object(res); }
  OB_INLINE void free_res(ObTableLSOpResult *res) { ls_res_pool_.free_object(res); }
private:
  int create_session_pool_safe();
  int create_session_pool_unsafe();
private:
  static const int64_t ELIMINATE_SESSION_DELAY = 5 * 1000 * 1000; // 5s
  static const int64_t SYS_VAR_REFRESH_DELAY = 5 * 1000 * 1000; // 5s
  static const int64_t ELIMINATE_RES_RESULT_DELAY = 5 * 1000 * 1000; // 5s
  static const int64_t REQUESE_RESULT_RETIRE_TIME = 60 * 1000 * 1000 ; // 60s
  bool is_inited_;
  common::ObArenaAllocator allocator_;
  ObTableApiSessPool *sess_pool_;
  ObSpinLock lock_; // for double check sess pool creating
  ObTableSessEliminationTask sess_elimination_task_;
  ObTableSessSysVarUpdateTask sys_var_update_task_;
  ObTableReqResEliminationTask req_res_elimination_task_;
  ObTableRelatedSysVars sys_vars_;
  ObTableObjectPool<ObTableLSOp> ls_op_pool_;
  ObTableObjectPool<ObTableLSOpResult> ls_res_pool_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTableObjectPoolMgr);
};

#define TABLEAPI_OBJECT_POOL_MGR (MTL(ObTableObjectPoolMgr*))

} // end namespace table
} // end namespace oceanbase

#endif /* OCEANBASE_OBSERVER_OB_TABLE_OBJECT_POOL_H_ */
