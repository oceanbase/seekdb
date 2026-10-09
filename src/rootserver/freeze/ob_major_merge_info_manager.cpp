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

#include <algorithm>
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "lib/time/ob_time_utility.h"
#include "rootserver/freeze/ob_major_merge_info_manager.h"
#include "rootserver/fork_table/instance_namespace_metadata.h"
#include "rootserver/freeze/namespace_freeze_preparation.h"
#include "query/api/query/session/ob_inner_sql_connection_access.h"

#include "share/ob_global_stat_proxy.h"
#include "share/ob_share_util.h"
#include "rootserver/ob_ddl_service.h"
#include "storage/tx/ob_ts_mgr.h"
#include "share/ob_structured_event_logger.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx_storage/ob_access_service.h"
#include "storage/instance_meta/instance_meta_store.h"

namespace oceanbase
{
using namespace common;
using namespace obcall;
using namespace share;
using namespace share::schema;
using namespace palf;

namespace rootserver
{

namespace
{
// The KV watermark commits before opening the SQL transaction. If the later
// SQL update fails, KV is ahead and rejects some otherwise valid pins; the
// reverse order could accept a pin after physical GC has passed it.
int advance_instance_snapshot_gc_watermark(const SCN &current, const SCN &next)
{
  auto *access = share::server_service<storage::ObAccessService>();
  if (access == nullptr) { return OB_NOT_INIT; }
  auto &kv = access->instance_meta_store();
  storage::InstanceMetaStore::Transaction tx;
  int ret = kv.begin(tx, ObTimeUtility::current_time() + 10 * 1000 * 1000);
  if (ret == OB_SUCCESS) {
    InstanceNamespaceMetadata metadata(kv, tx);
    int64_t watermark = 0;
    // Initialization uses a plain read and unique insert; advancement locks
    // the existing row in advance_snapshot_gc_watermark().
    ret = metadata.get_snapshot_gc_watermark(watermark);
    if (ret == OB_ENTRY_NOT_EXIST) {
      ret = metadata.initialize_snapshot_gc_watermark(current.get_val_for_tx());
      watermark = current.get_val_for_tx();
    }
    if (ret == OB_SUCCESS) {
      ret = metadata.advance_snapshot_gc_watermark(
          std::max(watermark, next.get_val_for_tx()));
    }
  }
  if (tx.is_active()) {
    const int end_ret = ret == OB_SUCCESS ? kv.commit(tx) : kv.rollback(tx);
    if (ret == OB_SUCCESS) { ret = end_ret; }
  }
  return ret;
}
} // namespace

/****************************** ObMajorMergeInfoManager ******************************/
int ObMajorMergeInfoManager::init(
    common::ObMySQLProxy &sql_proxy,
    share::schema::ObMultiVersionSchemaService &schema_service)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else if (OB_FAIL(global_merge_mgr_.init(sql_proxy))) {
  } else if (OB_FAIL(freeze_info_mgr_.init(sql_proxy))) {
  } else {
    sql_proxy_ = &sql_proxy;
    schema_service_ = &schema_service;
    is_inited_ = true;
  }

  return ret;
}

int ObMajorMergeInfoManager::try_reload()
{
  int ret = OB_SUCCESS;
  ObRecursiveMutexGuard guard(lock_);
  if (freeze_info_mgr_.is_valid()) {
    // do nothing
  } else if (OB_FAIL(reload())) {
  }
  return ret;
}

int ObMajorMergeInfoManager::reload(const bool force_reload_global_info)
{
  int ret = OB_SUCCESS;
  ObRecursiveMutexGuard guard(lock_);

  SCN global_broadcast_scn;
  if (force_reload_global_info && OB_FAIL(global_merge_mgr_.reload())) {
    LOG_WARN("fail to reload global merge info", KR(ret));
  } else if (!force_reload_global_info && OB_FAIL(global_merge_mgr_.try_reload())) {
    LOG_WARN("fail to try reload global merge info", KR(ret));
  } else if (OB_FAIL(global_merge_mgr_.get_global_broadcast_scn(global_broadcast_scn))) {
  } else if (OB_FAIL(freeze_info_mgr_.reload(global_broadcast_scn))) {
  } else {
    LOG_INFO("succ to reload merge info manager");
  }
  return ret;
}

int ObMajorMergeInfoManager::set_freeze_info(const ObMajorFreezeReason freeze_reason)
{
  auto *access = share::server_service<storage::ObAccessService>();
  if (access == nullptr) { return OB_NOT_INIT; }
  ObTimeoutCtx timeout;
  int ret = ObShareUtil::set_default_timeout_ctx(timeout, GCONF.internal_sql_execute_timeout);
  const int64_t deadline = timeout.get_abs_timeout();
  NamespaceFreezePreparation::Status status;
  int64_t last_diagnostic = 0;
  // Neither transaction locks nor this manager's mutex are held while the
  // existing materializer and takeover scheduler complete the inherited data.
  while (ret == OB_SUCCESS) {
    if (OB_FAIL(THIS_WORKER.check_status())) {
      break;
    }
    NamespaceFreezePreparation::Status checked;
    ret = NamespaceFreezePreparation::check(access->instance_meta_store(), deadline, checked);
    // Keep the last blocker when the next read cannot start before the deadline.
    if (ret == OB_SUCCESS || checked.namespace_id != 0) { status = checked; }
    if (ret != OB_SUCCESS && ret != OB_EAGAIN) { break; }
    if (ret == OB_SUCCESS && status.ready) {
      bool needs_recheck = false;
      ret = try_set_freeze_info(freeze_reason, deadline, needs_recheck);
      if (!needs_recheck || ret != OB_EAGAIN) { break; }
    }
    const int64_t now = ObTimeUtility::current_time();
    if (now >= deadline) { ret = OB_TIMEOUT; break; }
    if (now - last_diagnostic >= 1000 * 1000) {
      LOG_INFO("freeze waits for namespace baselines", K(ret), K(status), K(deadline));
      last_diagnostic = now;
    }
    ret = OB_SUCCESS;
    ob_usleep(std::min(int64_t(100000), deadline - now));
  }
  if (ret != OB_SUCCESS) {
    LOG_WARN("freeze preparation or publication failed", K(ret), K(status), K(deadline));
    if (ret == OB_TIMEOUT && !status.ready && status.namespace_id != 0) {
      OB_LOGGER.log_user_message(ObLogger::USER_ERROR, OB_TIMEOUT,
          "Freeze preparation timed out: namespace %lu, physical tablet %lu still needs %s; retry the freeze request",
          status.namespace_id, status.tablet_id,
          status.needs_materialization ? "materialization" : "local baseline completion");
    }
  }
  return ret;
}

// Add freeze info in one transaction, after a fresh locked readiness check.
int ObMajorMergeInfoManager::try_set_freeze_info(
    const ObMajorFreezeReason freeze_reason, int64_t deadline, bool &needs_recheck)
{
  int ret = OB_SUCCESS;
  needs_recheck = false;
  SCN new_frozen_scn;
  ObRecursiveMutexGuard guard(lock_);

  const int64_t fake_schema_version = 1000;
  SCN remote_snapshot_gc_scn;
  ObFreezeInfo freeze_info;
  storage::InstanceMetaStore::Transaction layout_reader;
  storage::InstanceMetaStore::Transaction creation_lock;
  auto *access = share::server_service<storage::ObAccessService>();

  if (access == nullptr) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(try_reload())) {
  } else {
    ObFreezeInfoProxy freeze_info_proxy{};
    // Keep the existing DDL publication coordination for the final baseline
    // recheck. Freeze publishes a target SCN, not a SQL schema version.
    ObDDLSQLTransaction trans(schema_service_, false/*need_end_signal*/, false/*stash*/, false/*parallel*/, false/*check_in_rs*/, false/*check_newest_schema*/);

    // In 'ddl_sql_transaction.start()', it implements the semantics of 'lock_all_ddl_operation'.
    if (OB_FAIL(trans.start(sql_proxy_, fake_schema_version))) {
    } else if (OB_FAIL(ObGlobalStatProxy::select_snapshot_gc_scn_for_update(
              trans, remote_snapshot_gc_scn))) {
    } else if (OB_FAIL(query::ObInnerSQLConnectionAccess::with_native_transaction(
        trans.get_connection(), [&](transaction::ObTxDesc &native) {
      auto &store = access->instance_meta_store();
      int rc = store.attach(creation_lock, native, deadline);
      if (rc == OB_SUCCESS) {
        rc = InstanceNamespaceMetadata(store, creation_lock).lock_namespace_creation();
      }
      return rc;
    }))) {
    } else {
      // attach() selected its snapshot BEFORE waiting for the allocation row.
      // check() starts a separate fresh read only view AFTER the lock. Never
      // take a Namespace root/watermark lock here: fork owns those first.
      NamespaceFreezePreparation::Status status;
      const int64_t check_start = ObTimeUtility::current_time();
      ret = NamespaceFreezePreparation::check(access->instance_meta_store(), deadline, status);
      needs_recheck = ret == OB_EAGAIN || (ret == OB_SUCCESS && !status.ready);
      if (needs_recheck) { ret = OB_EAGAIN; }
      LOG_INFO("freeze locked baseline recheck", K(ret), K(status),
          "cost_us", ObTimeUtility::current_time() - check_start);
      // 2. generate new frozen_scn
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(access->storage_schema_store().begin_read(layout_reader,
          deadline, [&](SCN &snapshot) {
        const int rc = generate_frozen_scn(remote_snapshot_gc_scn, snapshot);
        if (rc == OB_SUCCESS) { new_frozen_scn = snapshot; }
        return rc;
      }))) {
      } else {
        freeze_info.frozen_scn_ = new_frozen_scn;
        freeze_info.data_version_ = DATA_CURRENT_VERSION;
        // 4. insert freeze info
        if (OB_FAIL(freeze_info_proxy.set_freeze_info(trans, freeze_info))) {
        }
      }
    }

    ret = trans.handle_trans_in_the_end(ret);
  }

  if (creation_lock.is_active()) {
    const int end_ret = access->instance_meta_store().detach(creation_lock);
    if (ret == OB_SUCCESS) { ret = end_ret; }
  }

  // The SQL GC fence remains <= F until the freeze row commits. Subsequent
  // reload reads that fence before the freeze list, so this temporary reader
  // can leave without a gap or an extra persistent pin record (also on replay).
  if (layout_reader.is_active()) {
    const int end_ret = access->storage_schema_store().commit(layout_reader);
    if (ret == OB_SUCCESS) { ret = end_ret; }
  }

  if (FAILEDx(freeze_info_mgr_.add_freeze_info(freeze_info))) {
    LOG_WARN("fail to push back", KR(ret), K(freeze_info));
  }

  if (OB_FAIL(ret)) {
    freeze_info_mgr_.reset_freeze_info(); // reload freeze info on the next fetch
  }

  LOG_INFO("finish set freeze info", KR(ret), K(freeze_info));
  if (!needs_recheck) { MANAGEMENT_EVENT_ADD("major_merge", "root_major_freeze",
                        K(ret), "new_frozen_scn", new_frozen_scn.get_val_for_inner_table_field(),
                        "freeze_reason", major_freeze_reason_to_str(freeze_reason)); }
  return ret;
}

// lock guarded by caller
int ObMajorMergeInfoManager::generate_frozen_scn(
    const SCN &snapshot_gc_scn,
    SCN &new_frozen_scn)
{
  int ret = OB_SUCCESS;
  ObSnapshotTableProxy snapshot_proxy;
  ObSnapshotInfo snapshot_info;

  // build index or backup will acquire snapshot,
  // so should make sure frozen_scn will be greater max snapshot_ts.
  if (OB_FAIL(snapshot_proxy.get_max_snapshot_info(*sql_proxy_, snapshot_info))) {
   if (OB_ENTRY_NOT_EXIST == ret) {
     // no acquired snapshot
     ret = OB_SUCCESS;
   } else {
     LOG_WARN("fail to get max snapshot info", KR(ret));
   }
  }

  SCN tmp_frozen_scn;
  share::ObFreezeInfo latest_frozen_status;
  SCN local_max_frozen_scn;
  ObFreezeInfo max_frozen_status;
  ObFreezeInfoProxy freeze_info_proxy{};
  if (FAILEDx(freeze_info_proxy.get_max_freeze_info(*sql_proxy_, max_frozen_status))) {
    LOG_WARN("fail to get freeze info with max frozen_scn", KR(ret));
  } else if (OB_FAIL(freeze_info_mgr_.get_latest_freeze_info(latest_frozen_status))) {
  } else if (FALSE_IT(local_max_frozen_scn = latest_frozen_status.frozen_scn_)) {
  } else if (max_frozen_status.frozen_scn_ != local_max_frozen_scn) {
    // after new leader updates epoch and reloads freeze_info, old leader generates one new
    // frozen_scn and can add it into __all_freeze_info (cuz not checking epoch)
    // 
    if (local_max_frozen_scn < max_frozen_status.frozen_scn_) {
      ret = OB_EAGAIN;
      LOG_WARN("max frozen_scn in cache is smaller than max frozen_scn in table, will try again",
               KR(ret), K(local_max_frozen_scn), K(max_frozen_status));
    } else { // local_max_frozen_scn > max_frozen_status.frozen_scn_
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("max frozen_scn in cache is larger than max frozen_scn in table", KR(ret),
               K(local_max_frozen_scn), K(max_frozen_status));
    }
  } else if (OB_FAIL(get_gts(tmp_frozen_scn))) {
  } else if ((tmp_frozen_scn <= snapshot_gc_scn)
             || (tmp_frozen_scn <= local_max_frozen_scn)
             || (tmp_frozen_scn <= snapshot_info.snapshot_scn_)) {
    // current time from gts must be greater than old ts
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("get invalid frozen_timestmap", KR(ret), K(snapshot_gc_scn),
              K(tmp_frozen_scn), K(local_max_frozen_scn), K(snapshot_info));
  } else {
    new_frozen_scn = tmp_frozen_scn;
  }

  return ret;
}

int ObMajorMergeInfoManager::get_local_latest_frozen_scn(SCN &frozen_scn)
{
  int ret = OB_SUCCESS;
  ObRecursiveMutexGuard guard(lock_);
  share::ObFreezeInfo latest_freeze_info;

  if (OB_FAIL(try_reload())) {
  } else if (OB_FAIL(freeze_info_mgr_.get_latest_freeze_info(latest_freeze_info))) {
  } else {
    frozen_scn = latest_freeze_info.frozen_scn_;
  }
  return ret;
}

int ObMajorMergeInfoManager::renew_snapshot_gc_scn(SCN &new_snapshot_gc_scn)
{
  int ret = OB_SUCCESS;

  SCN cur_snapshot_gc_scn;
  SCN latest_snapshot_gc_scn;
  int64_t affected_rows = 0;
  ObMySQLTransaction trans;
  ObRecursiveMutexGuard guard(lock_);
  new_snapshot_gc_scn = SCN::min_scn();

  if (OB_FAIL(try_reload())) {
  }
  // no need to minus max_stale_time_for_weak_consistency since 4.1, because the collection of
  // multi-version data no longer depends on snapshot_gc_scn since 4.1
  else if (OB_FAIL(get_gts(new_snapshot_gc_scn))) {
  } else if (FALSE_IT(latest_snapshot_gc_scn = freeze_info_mgr_.get_snapshot_gc_scn())) {
  } else if (OB_FAIL(ObGlobalStatProxy::get_snapshot_gc_scn(
      *sql_proxy_, cur_snapshot_gc_scn))) {
  } else if ((new_snapshot_gc_scn <= latest_snapshot_gc_scn)
             || (cur_snapshot_gc_scn >= new_snapshot_gc_scn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid snaptshot gc time", KR(ret), K(cur_snapshot_gc_scn), K(new_snapshot_gc_scn),
      K(latest_snapshot_gc_scn));
  } else if (OB_FAIL(advance_instance_snapshot_gc_watermark(
      cur_snapshot_gc_scn, new_snapshot_gc_scn))) {
  } else if (OB_FAIL(trans.start(sql_proxy_))) {
  } else if (OB_FAIL(ObGlobalStatProxy::select_snapshot_gc_scn_for_update(
      trans, cur_snapshot_gc_scn))) {
  } else if (cur_snapshot_gc_scn >= new_snapshot_gc_scn) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObGlobalStatProxy::update_snapshot_gc_scn(trans, new_snapshot_gc_scn,
      affected_rows))) {
  } else if (!is_single_row(affected_rows)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("affected_rows expected to be one", KR(ret), K(affected_rows));
  } else if (OB_FAIL(freeze_info_mgr_.update_snapshot_gc_scn(new_snapshot_gc_scn))) {
  }

  ret = trans.handle_trans_in_the_end(ret);

  if (OB_FAIL(ret)) {
    freeze_info_mgr_.reset_freeze_info();
  }
  LOG_INFO("renew snapshot_gc_scn", K(ret), K(new_snapshot_gc_scn));

  return ret;
}

int ObMajorMergeInfoManager::try_gc_freeze_info()
{
  ObRecursiveMutexGuard guard(lock_);
  int ret = OB_SUCCESS;

  const int64_t MAX_KEEP_INTERVAL_NS =  30LL * 24 * 60 * 60 * 1000 * 1000 * 1000; // 30 day
  const int64_t MIN_REMAINED_VERSION_COUNT = 32;
  SCN cur_gts_scn;
  SCN min_frozen_scn;
  if (OB_FAIL(get_gts(cur_gts_scn))) {
  } else {
    min_frozen_scn = SCN::minus(cur_gts_scn, MAX_KEEP_INTERVAL_NS);
  }

  ObFreezeInfoProxy freeze_info_proxy{};
  ObMySQLTransaction trans;
  ObArray<ObFreezeInfo> all_freeze_info;
  SCN cur_snapshot_gc_scn;
  SCN completed_scn;

  if (FAILEDx(try_reload())) {
    LOG_WARN("fail to try reload", K(ret));
  } else if (OB_FAIL(global_merge_mgr_.get_global_last_merged_scn(completed_scn))) {
  } else if (OB_FAIL(trans.start(sql_proxy_))) {
  } else if (OB_FAIL(ObGlobalStatProxy::select_snapshot_gc_scn_for_update(trans, cur_snapshot_gc_scn))) {
  } else if (OB_FAIL(freeze_info_proxy.get_all_freeze_info(trans, all_freeze_info))) {
  } else {
    const int64_t freeze_info_cnt = all_freeze_info.count();
    if (freeze_info_cnt > MIN_REMAINED_VERSION_COUNT) {
      int64_t reserved_idx = freeze_info_cnt - MIN_REMAINED_VERSION_COUNT - 1;
      const SCN &tmp_frozen_scn = all_freeze_info.at(reserved_idx).frozen_scn_;

      min_frozen_scn = MIN(MIN(min_frozen_scn, tmp_frozen_scn), completed_scn);
      if (OB_FAIL(freeze_info_proxy.batch_delete(trans, min_frozen_scn))) {
      } else {
        // reload will later
        freeze_info_mgr_.reset_freeze_info();
        LOG_INFO("succ to batch delete freeze info", K(min_frozen_scn));
      }
    }
  }

  ret = trans.handle_trans_in_the_end(ret);
  return ret;
}

int ObMajorMergeInfoManager::try_reload_merge_info()
{
  int ret = OB_SUCCESS;
  ObRecursiveMutexGuard guard(lock_);

  if (OB_FAIL(try_reload())) {
  }
  return ret;
}

int ObMajorMergeInfoManager::inner_get_min_freeze_info(ObFreezeInfo &freeze_info)
{
  int ret = OB_SUCCESS;
  SCN global_last_merged_scn;

  if (OB_FAIL(try_reload())) {
  } else if (OB_FAIL(global_merge_mgr_.try_reload())) {
  } else if (OB_FAIL(global_merge_mgr_.get_global_last_merged_scn(global_last_merged_scn))) {
  } else if (OB_FAIL(freeze_info_mgr_.get_min_freeze_info_greater_than(
             global_last_merged_scn, freeze_info))) {
  }
  return ret;
}

int ObMajorMergeInfoManager::check_need_broadcast(bool &need_broadcast)
{
  int ret = OB_SUCCESS;
  ObRecursiveMutexGuard guard(lock_);

  ObFreezeInfo freeze_info;
  if (OB_FAIL(inner_get_min_freeze_info(freeze_info))) {
  } else if (freeze_info.is_valid()) {
    if (OB_FAIL(global_merge_mgr_.check_need_broadcast(freeze_info.frozen_scn_, need_broadcast))) {
    }
  }
  return ret;
}

int ObMajorMergeInfoManager::broadcast_freeze_info()
{
  int ret = OB_SUCCESS;
  ObRecursiveMutexGuard guard(lock_);

  ObFreezeInfo freeze_info;
  if (OB_FAIL(inner_get_min_freeze_info(freeze_info))) {
  } else if (freeze_info.is_valid()) {
    if (OB_FAIL(global_merge_mgr_.set_global_freeze_info(freeze_info.frozen_scn_))) {
    }
  }
  return ret;
}

int ObMajorMergeInfoManager::adjust_global_merge_info()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("merge info mgr not inited", KR(ret));
  } else if (OB_FAIL(global_merge_mgr_.adjust_global_merge_info())) {
  }
  return ret;
}

int ObMajorMergeInfoManager::get_gts(SCN &gts_scn) const
{
  int ret = OB_SUCCESS;
  const int64_t timeout_us = 10 * 1000 * 1000;

  if (OB_FAIL(OB_TS_MGR.get_gts_sync(timeout_us, gts_scn))) {
  }
  return ret;
}


} // rootserver
} // oceanbase
