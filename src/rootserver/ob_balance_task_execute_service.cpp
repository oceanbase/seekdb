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

#define USING_LOG_PREFIX BALANCE
#include "ob_balance_task_execute_service.h"
#include "share/balance/ob_balance_job_table_operator.h"//ObBalanceJob
#include "share/balance/ob_transfer_partition_task_table_operator.h"//set transfer
#include "share/ls/ob_ls_status_operator.h"//status_op
#include "share/location_cache/ob_location_service.h"//get_leader
#include "share/transfer/ob_transfer_task_operator.h"//get_history_task
#include "rootserver/balance/ob_ls_all_part_builder.h"   // ObLSAllPartBuilder
#include "rootserver/ob_disaster_recovery_task_utils.h"//DisasterRecoveryUtils

#define ISTAT(fmt, args...) FLOG_INFO("[BALANCE_EXECUTE] " fmt, ##args)
#define WSTAT(fmt, args...) FLOG_WARN("[BALANCE_EXECUTE] " fmt, ##args)


namespace oceanbase
{
using namespace common;
using namespace share;

namespace rootserver
{
//////////////ObBalanceTaskExecuteService
int ObBalanceTaskExecuteService::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", KR(ret));
#ifndef OB_BUILD_LITE
  } else if (OB_FAIL(ObTenantThreadHelper::create("BalanceExec",
          lib::TGDefIDs::SimpleLSService, *this))) {
    LOG_WARN("failed to create thread", KR(ret));
  } else if (OB_FAIL(ObTenantThreadHelper::start())) {
    LOG_WARN("fail to start thread", KR(ret));
#endif
  } else {
    sql_proxy_ = GCTX.sql_proxy_; 
    task_comment_.reset();
    tenant_id_ = MTL_ID();
    task_array_.reset();
    inited_ = true;
  }
  return ret;
}

void ObBalanceTaskExecuteService::destroy()
{
  ObTenantThreadHelper::destroy();
  tenant_id_ = OB_INVALID_TENANT_ID;
  task_comment_.reset();
  task_array_.reset();
  sql_proxy_ = NULL;
  inited_ = false;
}

int ObBalanceTaskExecuteService::wait_tenant_ready_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema ptr is null", KR(ret), KP(GCTX.schema_service_)); 
  } else {
    bool is_ready = false;
    while (!is_ready && !has_set_stop()) {
      ret = OB_SUCCESS;

      share::schema::ObSchemaGetterGuard schema_guard;
      const share::schema::ObTenantSchema *tenant_schema = NULL;
      if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(OB_SYS_TENANT_ID, schema_guard))) {
        LOG_WARN("fail to get schema guard", KR(ret));
      } else if (OB_FAIL(schema_guard.get_tenant_info(tenant_id_, tenant_schema))) {
        LOG_WARN("failed to get tenant ids", KR(ret), K(tenant_id_));
      } else if (OB_ISNULL(tenant_schema)) {
        ret = OB_TENANT_NOT_EXIST;
        LOG_WARN("tenant not exist", KR(ret), K(tenant_id_));
      } else if (!tenant_schema->is_normal()) {
        ret = OB_NEED_WAIT;
        WSTAT("tenant schema not ready, no need tenant balance", KR(ret));
      } else {
        is_ready = true;
      }

      if (! is_ready) {
        idle(10 * 1000 *1000);
      }
    }

    if (has_set_stop()) {
      WSTAT("thread has been stopped", K(is_ready), K(tenant_id_));
      ret = OB_IN_STOP_STATE;
    }
  }
  return ret;
}

int ObBalanceTaskExecuteService::try_update_task_comment_(
    const share::ObBalanceTask &task, const common::ObSqlString &comment,
    ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(inited_));
  } else if (OB_UNLIKELY(!task.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("job is invalid", KR(ret), K(task));
  } else if (comment.empty() || 0 == task.get_comment().string().case_compare(comment.ptr())) {
    //comment is empty or commet is same, no need to update
  } else if (OB_FAIL(ObBalanceTaskTableOperator::update_task_comment(tenant_id_,
      task.get_balance_task_id(), comment.string(), sql_client))) {
    LOG_WARN("failed to update task comment", KR(ret), K(task), K(comment), K(tenant_id_));
  }
  return ret;
}

void ObBalanceTaskExecuteService::do_work()
{
  int ret = OB_SUCCESS;
  ISTAT("balance task execute thread", K(tenant_id_));
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNDEFINED;
    LOG_WARN("sql proxy is null", KR(ret));
  } else if (OB_FAIL(wait_tenant_ready_())) {
    LOG_WARN("wait tenant ready fail", KR(ret), K(tenant_id_));
  } else {
    int64_t idle_time_us = 100 * 1000L;
    int tmp_ret = OB_SUCCESS;
    while (!has_set_stop()) {
      idle_time_us = 1 * 1000 * 1000L;
      ObCurTraceId::init(GCONF.self_addr_);
      task_array_.reset();
      DEBUG_SYNC(BEFORE_PROCESS_BALANCE_EXECUTE_WORK);
      //TODO, check schema ready
     if (OB_FAIL(ObBalanceTaskTableOperator::load_can_execute_task(
             tenant_id_, task_array_, *sql_proxy_))) {
        LOG_WARN("failed to load all balance task", KR(ret), K(tenant_id_));
      } else if (OB_FAIL(execute_task_())) {
        LOG_WARN("failed to execute balance task", KR(ret));
      }
      if (OB_FAIL(ret)) {
        idle_time_us = 100 * 1000;
      }
      ISTAT("finish one round", KR(ret), K(task_array_), K(idle_time_us), K(task_comment_));
      task_comment_.reset();
      idle(idle_time_us);
    }// end while
  }
}

int ObBalanceTaskExecuteService::finish_task_(
    const share::ObBalanceTask &task,
    const ObBalanceTaskStatus finish_task_status,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(! task.is_valid()
      || ! finish_task_status.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(task), K(finish_task_status));
  }
  // clean finish task, move to history
  else if (OB_FAIL(ObBalanceTaskTableOperator::clean_task(tenant_id_, task.get_balance_task_id(), trans))) {
    LOG_WARN("failed to clean task", KR(ret), K(tenant_id_), K(task));
  }
  // clean parent info of completed task
  // ignore failed status tasks
  else if (finish_task_status.is_completed()) {
    const ObBalanceTaskIDList &child_list = task.get_child_task_list();
    for (int64_t i = 0; OB_SUCC(ret) && i < child_list.count(); ++i) {
      if (OB_FAIL(ObBalanceTaskTableOperator::remove_parent_task(tenant_id_, child_list.at(i),
      task.get_balance_task_id(), trans))) {
        LOG_WARN("failed to clean parent info of task", KR(ret), K(child_list), K(i), K(task));
      }
    }
  }
  ISTAT("clean finished task", KR(ret), K(task), K(finish_task_status));
  return ret;
}

int ObBalanceTaskExecuteService::update_task_status_(
    const share::ObBalanceTask &task,
    const share::ObBalanceJobStatus &job_status,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObBalanceTaskStatus finish_task_status;
  bool task_is_finished = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(! task.is_valid()
      || !job_status.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(task), K(job_status));
  } else if (task.get_task_status().is_finish_status()) {
    task_is_finished = true;
    finish_task_status = task.get_task_status();
  } else {
    ObBalanceTaskStatus next_task_status = task.get_next_status(job_status);
    if (OB_UNLIKELY(!next_task_status.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("next task status is invalid", KR(ret), K(task));
    } else if (OB_FAIL(ObBalanceTaskTableOperator::update_task_status(
                   task, next_task_status, trans))) {
      LOG_WARN("failed to update task status", KR(ret), K(tenant_id_), K(task),
               K(next_task_status));
    } else {
      task_is_finished = (next_task_status.is_finish_status());
      // get latest task status
      finish_task_status = next_task_status;
    }
    ISTAT("update task status", KR(ret), "old_status", task.get_task_status(),
          K(next_task_status), K(task_is_finished), K(task), K(job_status), K(task_comment_));
  }

      // finish task as soon as possible in one trans after task is finished
  if (OB_FAIL(ret)) {
  } else if (task_is_finished &&
             OB_FAIL(finish_task_(task, finish_task_status, trans))) {
    LOG_WARN("fail to finish task", KR(ret), K(finish_task_status),
             K(task));
  }
  return ret;
}

int ObBalanceTaskExecuteService::process_current_task_status_(
    const share::ObBalanceTask &task, const share::ObBalanceJob &job,
    ObMySQLTransaction &trans,
    bool &skip_next_status)
{
  int ret = OB_SUCCESS;
  skip_next_status = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!task.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("task is invalid", KR(ret), K(task));
  } else {
    if (task.get_task_status().is_init()) {
      DEBUG_SYNC(BEFORE_PROCESS_BALANCE_TASK_INIT);
      if (OB_FAIL(process_init_task_(task, trans, skip_next_status))) {
        LOG_WARN("failed to init trans", KR(ret), K(task));
      }
    } else if (task.get_task_status().is_create_ls()) {
      DEBUG_SYNC(BEFORE_PROCESS_BALANCE_TASK_CREATE_LS);
      if (OB_FAIL(wait_ls_to_target_status_(task.get_dest_ls_id(),
                                            share::OB_LS_NORMAL, skip_next_status))) {
        LOG_WARN("failed to wait ls to normal", KR(ret), K(task));
      }
    } else if (task.get_task_status().is_transfer()) {
      DEBUG_SYNC(BEFORE_PROCESS_BALANCE_TASK_TRANSFER);
      bool all_part_transfered = false;
      if (OB_FAIL(execute_transfer_in_trans_(task, job, trans, all_part_transfered))) {
        LOG_WARN("failed to execute transfer in trans", KR(ret), K(task), K(job));
      } else if (!all_part_transfered) {
        skip_next_status = true;
      } else if (task.get_task_type().is_merge_task()) {
        DEBUG_SYNC(BEFORE_DROPPING_LS_IN_BALANCE_MERGE_TASK);
        if (OB_FAIL(set_ls_to_dropping_(task.get_src_ls_id(), trans))) {
          LOG_WARN("failed to set ls dropping", KR(ret), K(task));
        }
      }
    } else if (task.get_task_status().is_alter_ls()) {
      DEBUG_SYNC(BEFORE_PROCESS_BALANCE_TASK_ALTER_LS);
      if (OB_FAIL(wait_alter_ls_(task, skip_next_status))) {
        LOG_WARN("failed to wait alter ls", KR(ret), K(task));
      }
    } else if (task.get_task_status().is_set_merge_ls()) {
      DEBUG_SYNC(BEFORE_PROCESS_BALANCE_TASK_SET_MERGE);
      if (OB_FAIL(set_ls_to_merge_(task, trans))) {
        LOG_WARN("failed to set ls to merge", KR(ret), K(task));
      }
    } else if (task.get_task_status().is_drop_ls()) {
      DEBUG_SYNC(BEFORE_PROCESS_BALANCE_TASK_DROP_LS);
      if (OB_FAIL(wait_ls_to_target_status_(task.get_src_ls_id(),
                                            share::OB_LS_WAIT_OFFLINE, skip_next_status))) {
        LOG_WARN("failed to wait to wait offline", KR(ret), K(task));
        if (OB_ENTRY_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          // ls status already drop end
        }
      }
    } else {
      ret = OB_ERR_UNDEFINED;
      LOG_WARN("unexpected task status", KR(ret), K(task));
    }
  }
  return ret;
}
ERRSIM_POINT_DEF(EN_SET_TASK_EXECUTE_TIMEOUT);
int ObBalanceTaskExecuteService::execute_task_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNDEFINED;
    LOG_WARN("sql proxy is null", KR(ret));
  } else {
    int tmp_ret = OB_SUCCESS;
    for (int64_t i = 0; OB_SUCC(ret) && i < task_array_.count(); ++i) {
      ObBalanceJob job;
      task_comment_.reset();
      bool skip_next_status = false;
      common::ObMySQLTransaction trans;
      const ObBalanceTask &task = task_array_.at(i);
      const ObBalanceTaskID task_id = task.get_balance_task_id();
      ObBalanceTask task_in_trans;//for update
      ObTimeoutCtx timeout_ctx;
      int64_t balance_task_execute_timeout = GCONF.internal_sql_execute_timeout + 100 * 1000 * 1000L; // +100s
      if (EN_SET_TASK_EXECUTE_TIMEOUT) {
        LOG_INFO("set task execute timout", K(balance_task_execute_timeout));
        balance_task_execute_timeout = 10 * 1000 * 1000;
      } 
      DEBUG_SYNC(BEFORE_EXECUTE_BALANCE_TASK);
      if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(timeout_ctx, balance_task_execute_timeout))) {
        LOG_WARN("failed to get rs default timeout ctx", KR(ret));
      } else if (OB_FAIL(trans.start(sql_proxy_, tenant_id_))) {
        LOG_WARN("failed to start trans", KR(ret), K(tenant_id_));
      } else if (OB_FAIL(get_balance_job_task_for_update_(task, job, task_in_trans, trans))) {
        LOG_WARN("failed to get job", KR(ret), K(task));
      } else if (task_in_trans.get_task_status().is_finish_status()) {
      } else {
        if (job.get_job_status().is_doing()) {
          if (OB_FAIL(process_current_task_status_(task_in_trans, job, trans, skip_next_status))) {
            LOG_WARN("failed to process current task status", KR(ret), K(task_in_trans));
          }
        } else if (job.get_job_status().is_canceling()) {
          if (OB_FAIL(cancel_current_task_status_(task_in_trans, job, trans, skip_next_status))) {
            LOG_WARN("failed to cancel current task", KR(ret), K(task_in_trans));
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("balance job status not expected", KR(ret), K(task_in_trans), K(job));
        }
      }
      if (!task_comment_.empty()) {
        if (FAILEDx(try_update_task_comment_(task_in_trans, task_comment_, trans))) {
          LOG_WARN("failed to update task commet", KR(ret), K(task_in_trans),
              K(task_comment_));
        }
      }

        // move on to next status or clean up
      if (OB_SUCC(ret) && !skip_next_status) {
        if (OB_FAIL(update_task_status_(task_in_trans, job.get_job_status(), trans))) {
          LOG_WARN("failed to update task status", KR(ret), K(task_in_trans), K(job));
        }
      }


      if (trans.is_started()) {
        if (OB_TMP_FAIL(trans.end(OB_SUCC(ret)))) {
          LOG_WARN("failed to end trans", KR(ret), K(tmp_ret));
          ret = OB_SUCC(ret) ? tmp_ret : ret;
        }
      }

      if (task_in_trans.get_task_status().is_set_merge_ls()) {
        DEBUG_SYNC(AFTER_BLOCK_TABLET_IN_WHEN_LS_MERGE);
      }
      ISTAT("process task", KR(ret), K(task_in_trans), K(job), K(task_comment_));
      //isolate error of each task
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObBalanceTaskExecuteService::get_balance_job_task_for_update_(
    const ObBalanceTask &task, ObBalanceJob &job, ObBalanceTask &task_in_trans,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  job.reset();
  task_in_trans.reset();;
  int64_t start_time = 0; //no use
  int64_t finish_time = 0; //no use
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNDEFINED;
    LOG_WARN("sql proxy is null", KR(ret));
  } else if (OB_UNLIKELY(!task.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("task is invalid", KR(ret), K(task));
  } else if (OB_FAIL(ObBalanceJobTableOperator::get_balance_job(tenant_id_, true,
                  trans, job, start_time, finish_time))) {
    LOG_WARN("failed to get balance job", KR(ret), K(tenant_id_));
  } else if (task.get_job_id() != job.get_job_id()) {
    ret = OB_ERR_UNEXPECTED;
    WSTAT("job not expected", KR(ret), K(task), K(job));
  } else if (OB_FAIL(ObBalanceTaskTableOperator::get_balance_task(tenant_id_,
          task.get_balance_task_id(), true, trans, task_in_trans, start_time, finish_time))) {
    LOG_WARN("failed to get balance task", KR(ret), K(tenant_id_), K(task));
  }
  return ret;
}

int ObBalanceTaskExecuteService::cancel_current_task_status_(
    const share::ObBalanceTask &task, const share::ObBalanceJob &job,
    ObMySQLTransaction &trans, bool &skip_next_status)
{
  int ret = OB_SUCCESS;
  skip_next_status = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!task.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("task is invalid", KR(ret), K(task));
  } else {
    if (task.get_task_status().is_transfer()) {
      if (!task.get_current_transfer_task_id().is_valid()) {
        //no transfer, no need to todo
      } else {
        //try to wait transfer end
      }
    } else {
      //init, create ls, alter_ls, drop ls, set_ls_merge
      //no need wait task end
    }
    if (OB_SUCC(ret) && !task.get_task_status().is_init()
        && task.get_task_type().is_merge_task() && !skip_next_status) {
      //rollback flag
      ObLSAttrOperator ls_op(tenant_id_, sql_proxy_);
      share::ObLSAttr ls_info;
      share::ObLSFlag flag;
      if (OB_FAIL(ls_op.get_ls_attr(task.get_src_ls_id(), true, trans, ls_info))) {
        LOG_WARN("failed to get ls attr", KR(ret), K(task));
        if (OB_ENTRY_NOT_EXIST == ret) {
          //while task in dropping status, ls may not exist
          ret = OB_SUCCESS;
        }
      } else if (!ls_info.get_ls_flag().is_block_tablet_in()) {
        ret = OB_ERR_UNDEFINED;
        LOG_WARN("ls must in block tablet in", KR(ret), K(ls_info));
      } else {
        share::ObLSFlag new_flag = ls_info.get_ls_flag();
        new_flag.clear_block_tablet_in();
        if (OB_FAIL(ls_op.update_ls_flag_in_trans(
                ls_info.get_ls_id(), ls_info.get_ls_flag(), new_flag, trans))) {
          LOG_WARN("failed to update ls flag", KR(ret), K(ls_info));
        }
      }
      ISTAT("rollback flag of the ls", KR(ret), K(ls_info));
    }
    ISTAT("cancel task", KR(ret), K(task), K(task_comment_));
    //clear other init task which parent_list not empty
    if (OB_FAIL(ret)) {
    } else if (skip_next_status) {
    } else if (OB_FAIL(task_comment_.assign_fmt("Canceled on %s status",
            task.get_task_status().to_str()))) {
      LOG_WARN("failed to assign fmt", KR(ret), K(task));
    } else if (OB_FAIL(cancel_other_init_task_(task, trans))) {
      LOG_WARN("failed to cancel other init task", KR(ret), K(task));
    }
  }
  return ret;
}
int ObBalanceTaskExecuteService::cancel_other_init_task_(
    const share::ObBalanceTask &task, ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObBalanceTaskArray task_array;
  int tmp_ret = OB_SUCCESS;
  ObSqlString comment;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!task.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("task is invalid", KR(ret), K(task));
  } else if (OB_FAIL(ObBalanceTaskTableOperator::get_job_cannot_execute_task(
    tenant_id_, task.get_job_id(), task_array, trans))) {
    LOG_WARN("failed to get job init task", KR(ret), K(tenant_id_), K(task));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < task_array.count(); ++i) {
      comment.reset();
      const ObBalanceTask &other_task = task_array.at(i); 
      if (!other_task.get_task_status().is_init()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("task parent not empty, must be init", KR(ret), K(other_task));
      } else {
        //set task status to failed
        if (OB_FAIL(comment.assign_fmt("Canceled due to parent task %ld being canceled",
                task.get_balance_task_id().id()))) {
          LOG_WARN("failed to assign fmt", KR(tmp_ret), K(task), K(other_task));
        } else if (OB_FAIL(try_update_task_comment_(other_task, comment, trans))) {
          LOG_WARN("failed to update task comment", KR(tmp_ret), KR(ret), K(task), K(comment));
        } else if (OB_FAIL(update_task_status_(
               other_task,
               share::ObBalanceJobStatus(
                 share::ObBalanceJobStatus::BALANCE_JOB_STATUS_CANCELING),
               trans))) {
          LOG_WARN("failed to update task status", KR(ret), K(other_task));
        }
      }
      ISTAT("cancel task", KR(ret), K(other_task), K(task_comment_));
    }
  }
  return ret;
}

int ObBalanceTaskExecuteService::process_init_task_(const ObBalanceTask &task,
                                                    ObMySQLTransaction &trans,
                                                    bool &skip_next_status)
{
  int ret = OB_SUCCESS;
  skip_next_status = false;
  ObLSAttrOperator ls_op(tenant_id_, sql_proxy_);
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!task.is_valid() || !task.get_task_status().is_init())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("task is invalid", KR(ret), K(task));
  } else if (task.get_task_type().is_split_task()) {
    //insert a ls attr
    share::ObLSAttr ls_info;
    share::ObLSFlag flag;
    SCN create_scn;
    share::ObLSAttr src_ls_info;
    // when ls_group_id of split task is 0, it means creating a duplicate ls
    // TODO: add new task type like split_dup_ls
    if (0 == task.get_ls_group_id()) {
      flag.set_duplicate();
    }
    if (OB_FAIL(wait_can_create_new_ls_(create_scn))) {
      LOG_WARN("failed to wait create new ls", KR(ret), K(tenant_id_));
      if (OB_NEED_WAIT == ret && !task_comment_.empty()) {
        //To ensure task_comment can be updated to the table, reset the error code first
        //but to skip the creation of the log stream, set skip_next_status equal to true
        ret = OB_SUCCESS;
        skip_next_status = true;
      }
    } else if (OB_FAIL(ls_op.get_ls_attr(task.get_src_ls_id(), false, trans, src_ls_info))) {
      LOG_WARN("failed to get ls attr", KR(ret), K(task));
    } else if (OB_FAIL(ls_info.init(task.get_dest_ls_id(), src_ls_info.get_ls_group_id(), flag,
                             share::OB_LS_CREATING, share::OB_LS_OP_CREATE_PRE, create_scn))) {
      LOG_WARN("failed to init new operation", KR(ret), K(create_scn), K(task),
          K(skip_next_status), K(task_comment_));
      //TODO msy164651
    } else if (OB_FAIL(ls_op.insert_ls(ls_info, share::NORMAL_SWITCHOVER_STATUS, &trans, true/*skip_dup_ls_check*/))) {
      LOG_WARN("failed to insert new operation", KR(ret), K(ls_info));
    }
    ISTAT("create new ls", KR(ret), K(ls_info), K(task));
  } else if (task.get_task_type().is_alter_task()) {
    share::ObLSAttr ls_info;
    if (OB_FAIL(ls_op.get_ls_attr(task.get_src_ls_id(), true, trans, ls_info))) {
      LOG_WARN("failed to get ls attr", KR(ret), K(task));
    } else if (ls_info.get_ls_group_id() == task.get_ls_group_id()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ls group is is same, no need alter", KR(ret), K(ls_info), K(task));
    } else if (OB_FAIL(ls_op.alter_ls_group_in_trans(ls_info,
            task.get_ls_group_id(), trans))) {
      LOG_WARN("failed to alter ls group in trans", KR(ret), K(ls_info), K(task));
    }
    ISTAT("alter ls group id", KR(ret), K(ls_info), K(task));
  } else if (task.get_task_type().is_merge_task()) {
    share::ObLSAttr ls_info;
    if (OB_FAIL(ls_op.get_ls_attr(task.get_src_ls_id(), true, trans, ls_info))) {
      LOG_WARN("failed to get ls attr", KR(ret), K(task));
    } else if (ls_info.get_ls_flag().is_block_tablet_in()) {
      ret = OB_ERR_UNDEFINED;
      LOG_WARN("ls already in block tablet in", KR(ret), K(ls_info));
    } else {
      share::ObLSFlag new_flag = ls_info.get_ls_flag();
      new_flag.set_block_tablet_in();
      if (OB_FAIL(ls_op.update_ls_flag_in_trans(
          ls_info.get_ls_id(),
          ls_info.get_ls_flag(),
          new_flag,
          trans))) {
        LOG_WARN("failed to update ls flag", KR(ret), K(ls_info));
      }
    }
    ISTAT("update ls flag", KR(ret), K(ls_info), K(task));
  } else if (task.get_task_type().is_transfer_task()) {
    //nothing todo
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task type is invalid", KR(ret), K(task));
  }
  return ret;
}

int ObBalanceTaskExecuteService::wait_ls_to_target_status_(const ObLSID &ls_id,
    const share::ObLSStatus ls_status, bool &skip_next_status)
{
  int ret = OB_SUCCESS;
  skip_next_status = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNDEFINED;
    LOG_WARN("sql proxy is null", KR(ret));
  } else if (OB_UNLIKELY(!ls_id.is_valid() || share::ls_is_empty_status(ls_status))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(ls_id), K(ls_status));
  } else {
    ObLSStatusInfo status_info;
    ObLSStatusOperator ls_status_op;
    if (OB_FAIL(ls_status_op.get_ls_status_info(tenant_id_, ls_id, status_info, *sql_proxy_))) {
      LOG_WARN("failed to get ls status info", KR(ret), K(tenant_id_), K(ls_id));
    } else if (ls_status == status_info.status_) {
      //nothing
    } else {
      skip_next_status = true;
      if (OB_FAIL(task_comment_.assign_fmt("Wait for status of LS %ld to change from %s to %s",
      ls_id.id(), share::ls_status_to_str(status_info.status_), share::ls_status_to_str(ls_status)))) {
        LOG_WARN("failed to assign fmt", KR(ret), K(ls_id), K(ls_status), K(status_info));
      }
      WSTAT("need wait, ls not in target status", KR(ret), K(ls_status), K(status_info), K(task_comment_));
    }
  }
  return ret;
}

int ObBalanceTaskExecuteService::wait_alter_ls_(const share::ObBalanceTask &task, bool &skip_next_status)
{
  int ret = OB_SUCCESS;
  skip_next_status = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", KR(ret));
  } else if (OB_UNLIKELY(!task.get_task_status().is_alter_ls())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(task));
  } else {
    ObLSStatusInfo status_info;
    ObLSStatusOperator ls_status_op;
    if (OB_FAIL(ls_status_op.get_ls_status_info(tenant_id_,
          task.get_src_ls_id(), status_info, *sql_proxy_))) {
      LOG_WARN("failed to get ls status info", KR(ret), K(tenant_id_), K(task));
    } else if (status_info.ls_group_id_ == task.get_ls_group_id()) {
      // dr service on leader of meta tenant 1 LS. it may not be local at the moment.
      // to save resources, try to wake it up local, not guarante success.
      if (OB_FAIL(DisasterRecoveryUtils::wakeup_local_service(gen_meta_tenant_id(tenant_id_)))) {
        LOG_WARN("fail to wake up", KR(ret));
      }
    } else {
      skip_next_status = true;
      if (OB_FAIL(task_comment_.assign_fmt("Wait for LS group id of LS %ld to change from %lu to %lu",
      task.get_src_ls_id().id(), status_info.ls_group_id_, task.get_ls_group_id()))) {
        LOG_WARN("failed to assign sql", KR(ret), K(task), K(status_info));
      }
      WSTAT("need wait, alter ls not ready", KR(ret), K(status_info), K(task), K(task_comment_));
    }
  }
  return ret;
}

int ObBalanceTaskExecuteService::execute_transfer_in_trans_(
    const ObBalanceTask &task, const share::ObBalanceJob &job,
    ObMySQLTransaction &trans,
    bool &all_part_transferred)
{
  int ret = OB_SUCCESS;
  return ret;
}


int ObBalanceTaskExecuteService::set_ls_to_merge_(const share::ObBalanceTask &task, ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  bool all_part_transferred = false;
  if (OB_UNLIKELY(!task.get_task_status().is_set_merge_ls())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(task));
  } else if (OB_FAIL(get_and_update_merge_ls_part_list_(trans, task, all_part_transferred))) {
    LOG_WARN("get and update merge ls part list failed", KR(ret), K(task), K(all_part_transferred));
  }
  return ret;
}

int ObBalanceTaskExecuteService::get_and_update_merge_ls_part_list_(
    ObMySQLTransaction &trans,
    const share::ObBalanceTask &task,
    bool &all_part_transferred)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  share::ObTransferPartList part_list;
  const ObLSID &src_ls = task.get_src_ls_id();
  schema::ObMultiVersionSchemaService *schemaS = GCTX.schema_service_;

  all_part_transferred = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(schemaS) || OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected", KR(ret), K(sql_proxy_), KP(GCTX.schema_service_));
  } else if (OB_UNLIKELY(!task.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(task));
  }
  // build all part on src LS
  else if (OB_FAIL(ObLSAllPartBuilder::build(tenant_id_, src_ls, *schemaS, *sql_proxy_, part_list))) {
    // need retry, it is normal
    if (OB_NEED_RETRY == ret) {
      LOG_WARN("build all part of src LS fail, need retry", KR(ret), K(src_ls), K(tenant_id_), K(task));
    } else {
      LOG_WARN("build all part of src LS fail", KR(ret), K(src_ls), K(tenant_id_), K(task));
    }
  } else if (0 == part_list.count()) {
    all_part_transferred = true;
    ISTAT("there is no partition on src ls, no need update part list", K(task),
        K(all_part_transferred), K(part_list));
  } else if (OB_FAIL(ObBalanceTaskTableOperator::update_task_part_list(
      tenant_id_,
      task.get_balance_task_id(),
      part_list,
      trans))) {
    LOG_WARN("failed to update merge ls part list", KR(ret), K(tenant_id_), K(task), K(part_list));
  }
  int64_t finish_time = ObTimeUtility::current_time();

  ISTAT("build all part list for src LS of merge task and update finish", KR(ret),
      "cost", finish_time - start_time,
      K_(tenant_id), K(src_ls),
      K(all_part_transferred),
      "part_list_count", part_list.count(),
      K(task));
  return ret;
}

int ObBalanceTaskExecuteService::set_ls_to_dropping_(const ObLSID &ls_id, ObMySQLTransaction &trans)
{
 int ret = OB_SUCCESS;
  ObLSAttrOperator ls_op(tenant_id_, sql_proxy_);
  //TODO exclusion lock of ls
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("task is invalid", KR(ret), K(ls_id));
  } else if (OB_FAIL(ls_op.update_ls_status_in_trans(ls_id, share::OB_LS_NORMAL,
                                                     share::OB_LS_DROPPING,
                                                     share::NORMAL_SWITCHOVER_STATUS,
                                                     trans))) {
    LOG_WARN("failed to update ls status", KR(ret), K(ls_id));
  }
  return ret;
}
//When a merge_ls is executing, it ensures that the log stream will definitely be pushed to the wait_offline state.
//The purpose here is not to check if the existing resources are sufficient to create a log stream, but only to ensure
//The create_scn of the log stream newly created in this round of job will definitely be greater than the offline_scn of the wait_offline log stream generated by the previous round of job.
ERRSIM_POINT_DEF(EN_SET_MAX_OFFLINE_SCN);
int ObBalanceTaskExecuteService::wait_can_create_new_ls_(share::SCN &create_scn)
{
  int ret = OB_SUCCESS;
  create_scn.reset();
  share::SCN offline_scn;
  int64_t offline_ls_count = 0;
  uint64_t cluster_version = GET_MIN_CLUSTER_VERSION();

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(get_max_offline_scn_(offline_scn, offline_ls_count))) {
    LOG_WARN("failed to get max offline scn", KR(ret));
  } else if (0 == offline_ls_count) {
    if (OB_FAIL(ObLSAttrOperator::get_tenant_gts(tenant_id_, create_scn))) {
      LOG_WARN("failed to get tenant gts", KR(ret), K(tenant_id_));
    }
  } else if (OB_UNLIKELY(!offline_scn.is_valid() || 0 > offline_ls_count)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("offline scn is invalid", KR(ret), K(offline_scn), K(offline_ls_count));
  } else {
    const int64_t start_time = ObTimeUtility::fast_current_time();
    const int64_t TIMEOUT = GCONF.rpc_timeout;
    //for test
    if (EN_SET_MAX_OFFLINE_SCN) {
      LOG_INFO("set offline scn to max", K(offline_scn));
      offline_scn.set_max();
    }
    do {
      if (ObTimeUtility::fast_current_time() - start_time > TIMEOUT) {
        ret = OB_NEED_WAIT;
        LOG_WARN("stmt is timeout", KR(ret), K(start_time), K(TIMEOUT),
            K(create_scn), K(offline_scn));
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(task_comment_.assign(
                "Wait timed out for GTS to exceed max offline scn for all LS"))) {
          LOG_WARN("failed to assign task comment", KR(tmp_ret), K(offline_scn));
        }
      } else if (OB_FAIL(ObLSAttrOperator::get_tenant_gts(tenant_id_, create_scn))) {
        LOG_WARN("failed to get tenant gts", KR(ret), K(tenant_id_));
      } else if (create_scn > offline_scn) {
        ret = OB_SUCCESS;
      } else {
        ret = OB_EAGAIN;
        LOG_WARN("create scn is smaller than offline scn, need wait", KR(ret),
            K(create_scn), K(offline_scn), K(offline_ls_count));
        // waiting 100ms
        ob_usleep(100L * 1000L);
      }
    } while (OB_EAGAIN == ret);
  }
  return ret;
}
//finish transfer partition task Only in the case of a match at the destination can it be done

int ObBalanceTaskExecuteService::get_max_offline_scn_(share::SCN &offline_scn, int64_t &offline_ls_count)
{
  int ret = OB_SUCCESS;
  offline_scn.reset();
  offline_ls_count = 0;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(sql_proxy_) || OB_ISNULL(GCTX.srv_rpc_proxy_)
      || OB_ISNULL(GCTX.location_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(sql_proxy_), KP(GCTX.srv_rpc_proxy_),
        KP(GCTX.location_service_));
  } else {
   ObGetLSReplayedScnProxy proxy(
        *GCTX.srv_rpc_proxy_, &obrpc::ObSrvRpcProxy::get_ls_replayed_scn);
    ObArray<int> return_code_array;
    if (OB_FAIL(get_ls_offline_scn_by_rpc_(proxy, offline_ls_count, return_code_array))) {
      LOG_WARN("failed to get ls offline scn", KR(ret));
    } else if (0 == offline_ls_count) {
      //nothing todo
    } else if (return_code_array.count() != offline_ls_count) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("offline count not equal to return code array", KR(ret),
          K(offline_ls_count), K(return_code_array));
    } else if (OB_FAIL(proxy.check_return_cnt(return_code_array.count()))) {
      LOG_WARN("fail to check return cnt", KR(ret),
          "return_cnt", return_code_array.count());
    } else {
      offline_scn.set_min();
      for (int64_t i = 0; OB_SUCC(ret) && i < return_code_array.count(); ++i) {
        if (OB_FAIL(return_code_array.at(i))) {
          LOG_WARN("send rpc is failed", KR(ret), K(i));
        } else {
          const obrpc::ObGetLSReplayedScnRes *result = proxy.get_results().at(i);
          if (OB_ISNULL(result)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("result is null", KR(ret), K(i));
          } else if (!result->get_offline_scn().is_valid()) {
            ret = OB_NEED_WAIT;
            LOG_WARN("offline scn is invalid", KR(ret), KPC(result));
            int tmp_ret = OB_SUCCESS;
            if (OB_TMP_FAIL(task_comment_.assign_fmt("Wait for LS %ld to write offline log",
                    result->get_ls_id().id()))) {
              LOG_WARN("failed to assign task comment", KR(tmp_ret), KPC(result));
            }
          } else if (result->get_offline_scn() > offline_scn) {
            offline_scn = result->get_offline_scn();
            LOG_INFO("get offline scn", K(offline_scn), KPC(result));
          }
        }
      }//end for
    }
  }

  return ret;
}

int ObBalanceTaskExecuteService::get_ls_offline_scn_by_rpc_(
    ObGetLSReplayedScnProxy &proxy,
    int64_t &offline_ls_count,
    ObIArray<int> &return_code_array)
{
  int ret = OB_SUCCESS;
  offline_ls_count = 0;
  ObArray<ObLSStatusInfo> status_info_array;
  ObLSStatusOperator ls_status_op;
  int tmp_ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(sql_proxy_) || OB_ISNULL(GCTX.srv_rpc_proxy_)
      || OB_ISNULL(GCTX.location_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(sql_proxy_), KP(GCTX.srv_rpc_proxy_),
        KP(GCTX.location_service_));
  } else if (OB_FAIL(ls_status_op.get_all_ls_status_by_order(tenant_id_,
          status_info_array, *sql_proxy_))) {
    LOG_WARN("failed to get ls status info array", KR(ret), K(tenant_id_));
  } else {
    obrpc::ObGetLSReplayedScnArg arg;
    ObAddr leader;
    ObTimeoutCtx ctx;
    if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, GCONF.rpc_timeout))) {
      LOG_WARN("fail to set timeout ctx", KR(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < status_info_array.count(); ++i) {
      ObLSStatusInfo &info = status_info_array.at(i);
      if (info.ls_is_dropping()) {
        //Log streams in dropping state exist, unsure if they are remnants from the previous load balancing task
        //Wait for dropping to become wait_offline first
        ret = OB_NEED_WAIT;
        LOG_WARN("has dropping ls, need wait", K(ret), K(info));
        if (OB_TMP_FAIL(task_comment_.assign_fmt("Wait for LS %ld in DROPPING status to become OFFLINE",
                info.ls_id_.id()))) {
          LOG_WARN("failed to assign task comment", KR(tmp_ret), K(info));
        }
      } else if (!info.ls_is_wait_offline()) {
        //Load balancing process will not have the tenant_dropping state, other states are not considered
      } else {
        offline_ls_count++;
        const int64_t timeout = ctx.get_timeout();
        if (OB_FAIL(arg.init(tenant_id_, info.ls_id_, false))) {
          LOG_WARN("failed to init arg", KR(ret), K(arg));
          //Actually there is no need for it to be the leader replica, it is just more convenient to compare on the leader, so as long as
          //Get it back without validation
        } else if (OB_FAIL(GCTX.location_service_->get_leader(
                GCONF.cluster_id, tenant_id_, info.ls_id_, false, leader))) {
          LOG_WARN("failed to get leader", KR(ret), K(tenant_id_), K(info));
        } else if (OB_FAIL(proxy.call(leader, timeout, tenant_id_, arg))) {
          LOG_WARN("failed to send rpc", KR(ret), K(leader), K(timeout),
              K(tenant_id_), K(arg));
        }
        if (OB_FAIL(ret)) {
          if (OB_TMP_FAIL(GCTX.location_service_->nonblock_renew(
                  GCONF.cluster_id, tenant_id_, info.ls_id_))) {
            LOG_WARN("failed to renew location", KR(ret), KR(tmp_ret), K(tenant_id_), K(info));
          }
        }
      }//end else
    }//end for
    if (0 == offline_ls_count) {
      //nothing todo
    } else if (OB_TMP_FAIL(proxy.wait_all(return_code_array))) {
      LOG_WARN("wait all batch result failed", KR(ret), KR(tmp_ret));
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }
  return ret;
}

int ObBalanceTaskExecuteService::load_finish_transfer_part_tasks_(
    const ObTransferTask &transfer_task,
    const share::ObBalanceJob &job,
    ObTransferPartList &new_finish_list,
    ObTransferPartitionTaskID &max_task_id,
    ObLSID &dest_ls, ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  new_finish_list.reset();
  max_task_id.reset();
  dest_ls.reset();
  ObArray<ObTransferPartitionTask> task_array;
  const ObTransferPartList &finish_list = transfer_task.get_part_list(); 
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!transfer_task.is_valid()
        || !transfer_task.get_status().is_completed_status()
        || !job.is_valid() || !job.get_job_type().is_transfer_partition()
        || 0 >= finish_list.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(transfer_task), K(job));
  } else if (OB_FAIL(ObTransferPartitionTaskTableOperator::load_part_list_task(
          tenant_id_, job.get_job_id(), finish_list, task_array, trans))) {
    LOG_WARN("failed to get part list task", KR(ret), K(tenant_id_),
        K(finish_list));
  } else if (0 == task_array.count()) {
    //All related task partitions may have been deleted, no need to process the transfer_partition table
  } else {
    // Check if all partition destinations are consistent
    for (int64_t i = 0; OB_SUCC(ret) && i < task_array.count(); ++i) {
      const ObTransferPartitionTask &task = task_array.at(i);
      if (dest_ls.is_valid() && task.get_dest_ls() != dest_ls) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("task need have same dest ls", KR(ret), K(task),
            K(dest_ls), K(task_array));
      } else if (OB_FAIL(new_finish_list.push_back(task.get_part_info()))) {
        LOG_WARN("failed to push back", KR(ret), K(task));
      } else {
        dest_ls = task.get_dest_ls();
        if (!max_task_id.is_valid() || max_task_id < task.get_task_id()) {
          max_task_id = task.get_task_id();
        }
      }
    }//end for
  }
  return ret;
}
//start can have multiple times

}
}

