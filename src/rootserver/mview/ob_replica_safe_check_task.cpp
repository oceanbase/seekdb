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

#define USING_LOG_PREFIX RS

#include "ob_replica_safe_check_task.h"
#include "storage/compaction/ob_tenant_tablet_scheduler.h"
#include "share/ob_global_stat_proxy.h"

namespace oceanbase
{
namespace rootserver
{

int ObMVMergeSCNInfoCache::get_ls_info(
    const share::ObLSID &ls_id,
    ObMVMergeSCNInfo *&ls_info)
{
  int ret = OB_SUCCESS;
  ls_info = NULL;
  if (!ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg invalid", KR(ret), K(ls_id));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < arr_.count(); i++) {
      ObMVMergeSCNInfo &info = arr_.at(i);
      if (!info.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("info is invalid", KR(ret), K(ls_id), K(info));
      } else if (info.ls_id_ == ls_id) {
        ls_info = &info;
        break;
      }
    }

    if (OB_SUCC(ret) && OB_ISNULL(ls_info)) {
      if (OB_FAIL(arr_.push_back(ObMVMergeSCNInfo(ls_id)))) {
        LOG_WARN("failed to push_back", KR(ret), K(ls_id));
      } else {
        ls_info = &arr_.at(arr_.count() - 1);
        LOG_INFO("add ls_info", KR(ret), K(ls_id), KPC(ls_info), KPC(this));
      }
    }
  }
  return ret;
}

int ObMVMergeSCNInfoCache::clear_deleted_ls_info(
    const share::SCN &merge_scn)
{
  int ret = OB_SUCCESS;
  if (!merge_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invalid", KR(ret), K(merge_scn), KPC(this));
  } else {
    for (int64_t i = arr_.count() - 1; OB_SUCC(ret) && i >= 0; i--) {
      ObMVMergeSCNInfo &info = arr_.at(i);
      if (!info.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("info is invalid", KR(ret), K(info), KPC(this));
      } else if (info.major_mv_merge_scn_publish_ > merge_scn) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("info.major_mv_merge_scn_publish_ is more than merge_scn", KR(ret), K(merge_scn), K(info), KPC(this));
      } else if (info.major_mv_merge_scn_publish_ < merge_scn) {
        arr_.remove(i);
        LOG_INFO("delete ls info", K(i), K(info), K(merge_scn), KPC(this));
      }
    }
  }
  return ret;
}

ObReplicaSafeCheckTask::ObReplicaSafeCheckTask()
  : status_(StatusType::PUBLISH_SCN),
    in_sched_(false),
    is_stop_(true),
    is_inited_(false),
    merge_scn_()
{
}

ObReplicaSafeCheckTask::~ObReplicaSafeCheckTask() {}

int ObReplicaSafeCheckTask::init()
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObReplicaSafeCheckTask init twice", KR(ret), KPC(this));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObReplicaSafeCheckTask::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObReplicaSafeCheckTask not init", KR(ret), KPC(this));
  } else {
    is_stop_ = false;
    if (!in_sched_ && OB_FAIL(schedule_task(CHECK_INTERVAL, false /*repeat*/))) {
      LOG_WARN("fail to schedule mlog maintenance task", KR(ret));
    } else {
      in_sched_ = true;
      LOG_INFO("ObReplicaSafeCheckTask started", KR(ret), KPC(this));
    }
  }
  return ret;
}

void ObReplicaSafeCheckTask::stop()
{
  is_stop_ = true;
  in_sched_ = false;
  cancel_task();
  LOG_INFO("ObReplicaSafeCheckTask stopped", KPC(this));
}

void ObReplicaSafeCheckTask::wait() { wait_task(); }
void ObReplicaSafeCheckTask::destroy()
{
  is_inited_ = false;
  is_stop_ = true;
  in_sched_ = false;
  cancel_task();
  wait_task();
  cleanup();
}

void ObReplicaSafeCheckTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObReplicaSafeCheckTask not init", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(is_stop_)) {
    // do nothing
  } else {
    switch (status_) {
      case StatusType::PUBLISH_SCN:
        if (OB_FAIL(publish_scn())) {
          LOG_WARN("fail to publish scn", KR(ret), KPC(this));
        }
        break;
      case StatusType::CHECK_END:
        if (OB_FAIL(check_end())) {
          LOG_WARN("fail to check end", KR(ret), KPC(this));
        }
        break;
      case StatusType::NOTICE_SAFE:
        if (OB_FAIL(notice_safe())) {
          LOG_WARN("fail to notice safe", KR(ret), KPC(this));
        }
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected status", KR(ret), KPC(this));
        break;
    }
    LOG_INFO("timer task finish", KR(ret), KPC(this));
  }
}

void ObReplicaSafeCheckTask::switch_status(StatusType new_status, int64_t delay)
{
  int ret = OB_SUCCESS;
  // row: old_status; col: new_status
  bool check_status[3][3] = {
    {1, 1, 0},
    {1, 1, 1},
    {1, 0, 1}
  };
  if (check_status[(int)status_][(int)new_status]) {
    status_ = new_status;
    if (in_sched_) {
      if (OB_FAIL(schedule_task(delay, false /*repeat*/))) {
        LOG_WARN("fail to schedule replica safe check task", KR(ret), KPC(this));
      }
    }
    LOG_INFO("replica safe check task switch_status", KR(ret), K(new_status), K(delay), KPC(this));
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("status error", KR(ret), K(new_status), K(delay), KPC(this));
    ob_abort();
  }
}

int ObReplicaSafeCheckTask::register_mds_in_trans(
    const transaction::ObTxDataSourceType type,
    const ObUpdateMergeScnArg &arg,
    common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  observer::ObInnerSQLConnection *conn = NULL;
  int MAX_MULTI_BUF_SIZE = 64;
  char buf[MAX_MULTI_BUF_SIZE];
  int64_t pos = 0;
  int64_t buf_len = arg.get_serialize_size();
  if (share::SYS_LS == arg.ls_id_ ||
      !arg.is_valid() ||
      (transaction::ObTxDataSourceType::MV_PUBLISH_SCN != type &&
       transaction::ObTxDataSourceType::MV_NOTICE_SAFE != type &&
       transaction::ObTxDataSourceType::MV_MERGE_SCN != type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invaild", KR(ret), K(type), K(arg));
  } else if (!trans.is_started()) {
    LOG_WARN("trans is not started", KR(ret), K(type), K(arg));
  } else if (OB_ISNULL(conn = dynamic_cast<observer::ObInnerSQLConnection *>
                       (trans.get_connection()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("conn is NULL", KR(ret));
  } else if (OB_FAIL(arg.serialize(buf, buf_len, pos))) {
    LOG_WARN("fail to serialize", KR(ret), K(arg));
  } else if (OB_FAIL(conn->register_multi_data_source(MTL_ID(), arg.ls_id_, type, buf, buf_len))) {
    LOG_WARN("fail to register_tx_data", KR(ret), K(arg), K(buf_len));
  }
  return ret;
}

int ObReplicaSafeCheckTask::do_multi_trans(
    const transaction::ObTxDataSourceType type,
    const ObUpdateMergeScnArg &arg)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  observer::ObInnerSQLConnection *conn = NULL;
  int MAX_MULTI_BUF_SIZE = 64;
  char buf[MAX_MULTI_BUF_SIZE];
  int64_t pos = 0;
  int64_t buf_len = arg.get_serialize_size();
  if (share::SYS_LS == arg.ls_id_ || !arg.is_valid()
      || (transaction::ObTxDataSourceType::MV_PUBLISH_SCN != type
          && transaction::ObTxDataSourceType::MV_NOTICE_SAFE != type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invaild", KR(ret), K(type), K(arg));
  } else if (buf_len > MAX_MULTI_BUF_SIZE) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("status error", KR(ret), K(buf_len), K(MAX_MULTI_BUF_SIZE), KPC(this));
    ob_abort();
  } else if (OB_FAIL(trans.start(GCTX.sql_proxy_, MTL_ID()))) {
    LOG_WARN("failed to start trans", KR(ret), KPC(this));
  } else if (OB_ISNULL(conn = dynamic_cast<observer::ObInnerSQLConnection *>
                       (trans.get_connection()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("conn is NULL", KR(ret), KPC(this));
  } else if (OB_FAIL(arg.serialize(buf, buf_len, pos))) {
    LOG_WARN("fail to serialize", KR(ret), K(arg));
  } else if (OB_FAIL(conn->register_multi_data_source(MTL_ID(), arg.ls_id_, type, buf, buf_len))) {
    LOG_WARN("fail to register_tx_data", KR(ret), K(arg), K(buf_len));
  }
  if (trans.is_started()) {
    int temp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
      LOG_WARN("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
      ret = (OB_SUCC(ret)) ? temp_ret : ret;
    }
  }
  return ret;
}

int ObReplicaSafeCheckTask::publish_scn()
{
  int ret = OB_SUCCESS;
  switch_status(StatusType::PUBLISH_SCN, CHECK_INTERVAL);
  return ret;
}

int ObReplicaSafeCheckTask::check_row_empty(
      const ObSqlString &sql_str,
      bool &is_empty)
{
  int ret = OB_SUCCESS;
  is_empty = false;
  SMART_VAR(ObMySQLProxy::MySQLResult, res)
  {
    common::sqlclient::ObMySQLResult *result = nullptr;
    if (!sql_str.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("sql_str is invalid", KR(ret));
    } else if (OB_FAIL(GCTX.sql_proxy_->read(res, MTL_ID(), sql_str.ptr()))) {
      LOG_WARN("execute sql failed", KR(ret), K(sql_str));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result is null", KR(ret));
    } else if (OB_FAIL(result->next())) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        LOG_WARN("fail to get next", KR(ret));
      } else {
        is_empty = true;
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
}

int ObReplicaSafeCheckTask::check_end()
{
  int ret = OB_SUCCESS;
  bool no_new_create = false;
  ObSqlString check_new_create_sql;
  if (OB_FAIL(check_new_create_sql.assign_fmt("SELECT 1 FROM %s WHERE last_refresh_scn = 0 AND refresh_mode = %ld limit 1", share::OB_ALL_MVIEW_TNAME, ObMVRefreshMode::MAJOR_COMPACTION))) {
    LOG_WARN("failed to assign sql", KR(ret), K(check_new_create_sql), KPC(this));
  } else if (OB_FAIL(check_row_empty(check_new_create_sql, no_new_create))) {
    LOG_WARN("failed to check_row_empty", KR(ret), K(check_new_create_sql), KPC(this));
  } else if (!no_new_create) {
  }
  LOG_INFO("check_end finish", KR(ret), K(no_new_create), KPC(this));
  if (OB_FAIL(ret)) {
    switch_status(StatusType::PUBLISH_SCN, ERROR_RETRY_INTERVAL);
  } else if (!no_new_create) {
    switch_status(StatusType::CHECK_END, WAIT_END_INTERVAL);
  } else {
    switch_status(StatusType::NOTICE_SAFE);
  }
  return ret;
}

int ObReplicaSafeCheckTask::notice_safe()
{
  int ret = OB_SUCCESS;
  // merge version and tenant scn
  if (ls_cache_.get_ls_info_cnt() == 0
      || !merge_scn_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task ctx is unexpected", KR(ret), KPC(this));
  } else {
    ObArray<ObMVMergeSCNInfo>& ls_infos = ls_cache_.get_ls_infos();
    for (int64_t i = 0; OB_SUCC(ret) && i < ls_infos.count(); ++i) {
      ObMVMergeSCNInfo &ls_info = ls_infos.at(i);
      if (!ls_info.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls_info is invalid", KR(ret), K(ls_info), KPC(this));
      } else if (merge_scn_ != ls_info.major_mv_merge_scn_publish_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("merge_scn is not equal ls_info.major_mv_merge_scn_publish_", KR(ret), K(ls_info), KPC(this));
      } else if (ls_info.major_mv_merge_scn_safe_calc_ < ls_info.major_mv_merge_scn_publish_) {
        ObUpdateMergeScnArg arg;
        arg.merge_scn_ = merge_scn_;
        arg.ls_id_ = ls_info.ls_id_;
        if (OB_FAIL(do_multi_trans(transaction::ObTxDataSourceType::MV_NOTICE_SAFE, arg))) {
          LOG_WARN("failed to do_multi_trans", KR(ret), K(ls_info), K(arg), K(this));
        } else {
          ls_info.major_mv_merge_scn_safe_calc_ = ls_info.major_mv_merge_scn_publish_;
        }
      }
    }
  }
  LOG_INFO("notice_safe finish", KR(ret), KPC(this));

  if (OB_LS_LOCATION_LEADER_NOT_EXIST == ret || OB_NOT_MASTER == ret) {
    switch_status(StatusType::NOTICE_SAFE, LOCATION_RETRY_INTERVAL);
  } else if (OB_FAIL(ret)) {
    switch_status(StatusType::PUBLISH_SCN, ERROR_RETRY_INTERVAL);
  } else {
    switch_status(StatusType::PUBLISH_SCN, CHECK_INTERVAL);
  }
  return ret;
}

int ObReplicaSafeCheckTask::create_ls_with_tenant_mv_merge_scn(const uint64_t tenant_id,
                                                               const share::ObLSID &ls_id,
                                                               common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  share::SCN merge_scn(share::SCN::min_scn());

  if (tenant_id == OB_INVALID_TENANT_ID ||
      !ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(ls_id));
  } else if (ls_id.is_sys_ls()) {
    // do nothing
  } else if (!trans.is_started()) {
    LOG_WARN("trans not start", KR(ret), K(tenant_id));
  } else {
    ObGlobalStatProxy proxy(trans, tenant_id);
    if (OB_FAIL(proxy.get_major_refresh_mv_merge_scn(false /* for_update */, merge_scn))) {
      LOG_WARN("fail to get major_refresh_mv_merge_scn", KR(ret), K(tenant_id));
      if (OB_ERR_NULL_VALUE == ret) {
        ret = OB_SUCCESS;
        merge_scn.set_min();
      }
    }
    if (OB_SUCC(ret) && !merge_scn.is_min()) { // skip min merge scn
      ObUpdateMergeScnArg arg;
      if (OB_FAIL(arg.init(ls_id, merge_scn))) {
        LOG_WARN("failed to init arg", KR(ret), K(ls_id), K(merge_scn));
      } else if (OB_FAIL(register_mds_in_trans(transaction::ObTxDataSourceType::MV_PUBLISH_SCN, arg, trans))) {
        LOG_WARN("failed to do multi trans", KR(ret), K(arg));
      } else if (OB_FAIL(register_mds_in_trans(transaction::ObTxDataSourceType::MV_NOTICE_SAFE, arg, trans))) {
        LOG_WARN("failed to do multi trans", KR(ret), K(arg));
      } else if (OB_FAIL(register_mds_in_trans(transaction::ObTxDataSourceType::MV_MERGE_SCN, arg, trans))) {
        LOG_WARN("failed to do multi trans", KR(ret), K(arg));
      }
    }
  }
  LOG_INFO("create ls with tenant mv merge scn", K(ret), K(merge_scn), K(tenant_id), K(ls_id));
  return ret;
}
// void ObReplicaSafeCheckTask::finish()
// {
//   int ret = OB_SUCCESS;
//   LOG_INFO("replica safe check task finish", KPC(this));
//   // cleanup
//   cleanup();
//   // schedule next round
//   switch_status(StatusType::PUBLISH_SCN, CHECK_INTERVAL);
// }

void ObReplicaSafeCheckTask::cleanup()
{
  status_ = StatusType::PUBLISH_SCN;
  merge_scn_.reset();
  ls_cache_.reset();
}

} // namespace rootserver
} // namespace oceanbase
