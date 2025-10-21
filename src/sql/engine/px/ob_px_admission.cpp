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

#define USING_LOG_PREFIX SQL
#include "ob_px_admission.h"
#include "observer/omt/ob_tenant.h"
#include "ob_px_target_mgr.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

int ObPxAdmission::get_parallel_session_target(ObSQLSessionInfo &session,
                                               int64_t minimal_session_target,
                                               int64_t &session_target)
{
  int ret = OB_SUCCESS;
  int64_t parallel_servers_target = INT64_MAX; // default to unlimited
  session_target = INT64_MAX; // default to unlimited
  uint64_t tenant_id = session.get_effective_tenant_id();
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF(tenant_id));
  if (OB_FAIL(OB_PX_TARGET_MGR.get_parallel_servers_target(tenant_id, parallel_servers_target))) {
    LOG_WARN("get parallel_servers_target failed", K(ret));
  } else if (OB_UNLIKELY(minimal_session_target > parallel_servers_target)) {
    ret = OB_ERR_PARALLEL_SERVERS_TARGET_NOT_ENOUGH;
    LOG_WARN("minimal_session_target is more than parallel_servers_target", K(ret),
                                      K(minimal_session_target), K(parallel_servers_target));
  } else if (OB_LIKELY(tenant_config.is_valid())) {
    session_target = parallel_servers_target;
    int64_t pmas = tenant_config->_parallel_max_active_sessions;
    int64_t parallel_session_count;
    if (OB_FAIL(OB_PX_TARGET_MGR.get_parallel_session_count(tenant_id, parallel_session_count))) {
      LOG_WARN("get parallel_px_session failed", K(ret));
    } else if (pmas > 0 && parallel_servers_target != INT64_MAX && parallel_session_count > 0) {
      // when pmas is TOO large, session target could be less than one,
      // this is not good! We ensure this query can run with minimal threads here
      session_target = std::max(parallel_servers_target / pmas, minimal_session_target);
    }
  } else {
    // tenant_config is invalid, use parallel_servers_target
    session_target = parallel_servers_target;
  }
  LOG_TRACE("PX get parallel session target", K(tenant_id), K(tenant_config.is_valid()),
                        K(parallel_servers_target), K(minimal_session_target), K(session_target));
  return ret;
}
// If the current remaining number of threads can meet req_cnt, then allocate threads to the request
// But considering that the system should allow the first request to execute when idle, need to handle the following special cases:
//   If the number of request threads req_cnt is greater than limit, and there are no other px requests currently (used = 0)
//   Then assign all threads to this request (admit_cnt = req_cnt, used = limit)
//
//   Inference: A request that requires **excess** threads will only be scheduled after the system becomes idle
int64_t ObPxAdmission::admit(ObSQLSessionInfo &session, ObExecContext &exec_ctx,
                             int64_t wait_time_us, int64_t minimal_px_worker_count,
                             int64_t &session_target, ObHashMap<ObAddr, int64_t> &worker_map,
                             int64_t req_cnt, int64_t &admit_cnt)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = session.get_effective_tenant_id();
  uint64_t admission_version = UINT64_MAX;
  // when pmas enabled, block thread until got expected thread resource
  int64_t left_time_us = wait_time_us;
  int64_t start_time_us = ObClockGenerator::getClock();
  bool need_retry = false;
  do {
    if (OB_FAIL(THIS_WORKER.check_status())) {
      LOG_WARN("fail check query status", K(ret));
    } else if (OB_FAIL(OB_PX_TARGET_MGR.apply_target(tenant_id, worker_map, wait_time_us, session_target, req_cnt, admit_cnt, admission_version))) {
      LOG_WARN("apply target failed", K(ret), K(tenant_id), K(req_cnt));
    } else if (0 != admit_cnt) {
      exec_ctx.set_admission_version(admission_version);
      LOG_TRACE("after enter admission", K(ret), K(req_cnt), K(admit_cnt));
    }
    left_time_us = wait_time_us - (ObClockGenerator::getClock() - start_time_us);
    if (OB_SUCC(ret) && 0 == admit_cnt && left_time_us > 0) {
      if (!need_retry) {
        // only print once
        LOG_INFO("Not enough PX thread to execute query."
                 "should wait and re-acquire thread resource from target queue",
                 K(req_cnt), K(left_time_us));
        // fake one retry record, not really a query retry
        session.get_retry_info_for_update().set_last_query_retry_err(OB_ERR_INSUFFICIENT_PX_WORKER);
      }
      // parallel server target may changed
      if (OB_FAIL(get_parallel_session_target(session, minimal_px_worker_count, session_target))) {
        LOG_WARN("fail get session target", K(ret));
      } else {
        need_retry = true;
      }
    } else {
      need_retry = false;
    }
  } while (need_retry && OB_SUCC(ret));
  return ret;
}

int ObPxAdmission::enter_query_admission(ObSQLSessionInfo &session,
                                         ObExecContext &exec_ctx,
                                         sql::stmt::StmtType stmt_type,
                                         ObPhysicalPlan &plan)
{
  int ret = OB_SUCCESS;
  // For the scenario where only dop=1, skip the check, because this scenario goes through the RPC thread and does not consume PX threads
  // 
  if (stmt::T_EXPLAIN != stmt_type
      && plan.is_use_px()
      && 1 != plan.get_px_dop()
      && plan.get_expected_worker_count() > 0) {
    // use for appointment
    const auto &req_px_worker_map = plan.get_expected_worker_map();
    ObHashMap<ObAddr, int64_t> &acl_px_worker_map = exec_ctx.get_admission_addr_map();
    if (acl_px_worker_map.created()) {
      acl_px_worker_map.clear();
    } else if (OB_FAIL(acl_px_worker_map.create(hash::cal_next_prime(10), ObModIds::OB_SQL_PX, ObModIds::OB_SQL_PX))){
      LOG_WARN("create hash map failed", K(ret));
    }
    if (OB_SUCC(ret)) {
      for (auto it = req_px_worker_map.begin(); 
          OB_SUCC(ret) && it != req_px_worker_map.end(); ++it) {
        if (OB_FAIL(acl_px_worker_map.set_refactored(it->first, it->second))){
          LOG_WARN("set refactored failed", K(ret), K(it->first), K(it->second));
        }
      }
      // use for exec
      int64_t req_worker_count = plan.get_expected_worker_count();
      int64_t minimal_px_worker_count = plan.get_minimal_worker_count();
      int64_t admit_worker_count = 0;
      // If thread resources are not obtained for a long time, a timeout exit is required.
      // Below processing the scenario with timeout hint
      if (plan.get_phy_plan_hint().query_timeout_ > 0) {
        THIS_WORKER.set_timeout_ts(
            session.get_query_start_time() + plan.get_phy_plan_hint().query_timeout_);
      }
      int64_t wait_time_us = THIS_WORKER.get_timeout_remain();
      int64_t session_target = INT64_MAX;
      if (OB_FAIL(get_parallel_session_target(session, minimal_px_worker_count, session_target))) {
        LOG_WARN("fail get session target", K(ret));
      } else if (OB_FAIL(THIS_WORKER.check_status())) {
        LOG_WARN("fail check query status", K(ret));
      } else if (OB_FAIL(ObPxAdmission::admit(session, exec_ctx,
                                              wait_time_us, minimal_px_worker_count, session_target,
                                              acl_px_worker_map, req_worker_count, admit_worker_count))) {
        LOG_WARN("fail do px admission",
                K(ret), K(wait_time_us), K(session_target));
      } else if (admit_worker_count <= 0) {
        plan.inc_delayed_px_querys();
        ret = OB_ERR_INSUFFICIENT_PX_WORKER;
        ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(dop_, plan.get_px_dop());
        ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(required_px_workers_number_, req_worker_count);
        ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(admitted_px_workers_number_, admit_worker_count);
        LOG_INFO("This query is out of px worker resources and needs to be delayed; "
                "disconnection is unnecessary.",
                K(admit_worker_count),
                K(plan.get_px_dop()),
                K(plan.get_plan_id()),
                K(ret));
      } else {
        ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(exec_ctx);
        ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(exec_ctx);
        if (OB_ISNULL(plan_ctx) || OB_ISNULL(task_exec_ctx)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          plan_ctx->set_worker_count(admit_worker_count);
          // indicates the number calculated by optimizer
          task_exec_ctx->set_expected_worker_cnt(req_worker_count);
          task_exec_ctx->set_minimal_worker_cnt(minimal_px_worker_count);
          // Indicates the actual number of allocations made by admission based on the current resource queue situation
          task_exec_ctx->set_admited_worker_cnt(admit_worker_count);
        }
        LOG_TRACE("PX admission set the plan worker count", K(req_worker_count), K(minimal_px_worker_count), K(admit_worker_count));
      }
    }
  }
  if (stmt::T_EXPLAIN != stmt_type && plan.get_das_dop() > 0) {
    int64_t minimal_px_worker_count = plan.get_minimal_worker_count();
    int64_t parallel_servers_target = INT64_MAX;
    if (OB_FAIL(OB_PX_TARGET_MGR.get_parallel_servers_target(session.get_effective_tenant_id(),
                                                             parallel_servers_target))) {
      LOG_WARN("get parallel_servers_target failed", K(ret));
    } else {
      int64_t real_das_dop = std::min(parallel_servers_target, plan.get_das_dop());
      exec_ctx.get_das_ctx().set_real_das_dop(real_das_dop);
      LOG_TRACE("real das dop", K(real_das_dop), K(plan.get_das_dop()), K(parallel_servers_target));
    }
  }
  return ret;
}

void ObPxAdmission::exit_query_admission(ObSQLSessionInfo &session,
                                         ObExecContext &exec_ctx,
                                         sql::stmt::StmtType stmt_type,
                                         ObPhysicalPlan &plan)
{
  if (stmt::T_EXPLAIN != stmt_type
      && plan.is_use_px()
      && 1 != plan.get_px_dop()
      && exec_ctx.get_admission_version() != UINT64_MAX) {
    int ret = OB_SUCCESS;
    uint64_t tenant_id = session.get_effective_tenant_id();
    hash::ObHashMap<ObAddr, int64_t> &addr_map = exec_ctx.get_admission_addr_map();
    if (OB_FAIL(OB_PX_TARGET_MGR.release_target(tenant_id,
                                                addr_map,
                                                exec_ctx.get_admission_version()))) {
      LOG_WARN("release target failed", K(ret), K(tenant_id), K(exec_ctx.get_admission_version()));
    }
    (void)addr_map.destroy();
    LOG_DEBUG("release resource, notify wait threads");
  }
}
// Supply SQC end used Admission module
// Each tenant one resource pool
void ObPxSubAdmission::acquire(int64_t max, int64_t min, int64_t &acquired_cnt)
{
  UNUSED(min);
  oceanbase::omt::ObTenant *tenant = nullptr;
  oceanbase::omt::ObThWorker *worker = nullptr;
  int64_t upper_bound = 1;
  if (nullptr == (worker = THIS_THWORKER_SAFE)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "Oooops! can't find tenant. Unexpected!", K(max), K(min));
  } else if (nullptr == (tenant = worker->get_tenant())) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "Oooops! can't find tenant. Unexpected!", KP(worker), K(max), K(min));
  } else {
    oceanbase::omt::ObTenantConfigGuard tenant_config(TENANT_CONF(tenant->id()));
    if (!tenant_config.is_valid()) {
      LOG_WARN_RET(OB_ERR_UNEXPECTED, "get tenant config failed, use default cpu_quota_concurrency");
      upper_bound = tenant->unit_min_cpu() * 4;
    } else {
      upper_bound = tenant->unit_min_cpu() * tenant_config->_max_px_workers_per_cpu;
    }
  }
  acquired_cnt = std::min(max, upper_bound);
}

void ObPxSubAdmission::release(int64_t acquired_cnt)
{
  UNUSED(acquired_cnt);
}
