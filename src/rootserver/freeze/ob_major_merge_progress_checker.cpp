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

#define USING_LOG_PREFIX RS_COMPACTION
#include "rootserver/freeze/ob_major_merge_progress_checker.h"
#include "rootserver/freeze/ob_checksum_validator.h"
#include "rootserver/fork_table/instance_namespace_metadata.h"
#include "rootserver/fork_table/table_storage_layouts.h"
#include "observer/namespace_worker_protocol_prototype.h"
#include "namespace/namespace.h"
#include "storage/tx_storage/ob_access_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ls/ob_ls.h"
#include "share/rc/ob_server_runtime.h"
#include "lib/worker.h"

namespace oceanbase {
namespace rootserver {
using namespace common;
using namespace share;
using namespace storage;
using namespace compaction;

int ObMajorMergeProgressChecker::init(bool is_primary_service)
{
  if (initialized_) { return OB_INIT_TWICE; }
  primary_ = is_primary_service;
  initialized_ = true;
  return OB_SUCCESS;
}

int ObMajorMergeProgressChecker::set_basic_info(const ObFreezeInfo &freeze_info)
{
  if (!initialized_) { return OB_NOT_INIT; }
  if (!freeze_info.frozen_scn_.is_valid() || freeze_info.frozen_scn_.is_min()) { return OB_INVALID_ARGUMENT; }
  if (freeze_.frozen_scn_ != freeze_info.frozen_scn_) {
    clear_cached_info();
    freeze_ = freeze_info;
  }
  return OB_SUCCESS;
}

int ObMajorMergeProgressChecker::clear_cached_info()
{
  completed_.clear();
  progress_.reset();
  pending_.reset();
  freeze_.reset();
  return OB_SUCCESS;
}

int ObMajorMergeProgressChecker::check_progress()
{
  int ret = OB_SUCCESS;
  auto *access = share::server_service<ObAccessService>();
  auto *ls_service = share::server_service<ObLSService>();
  ObLS *ls = nullptr;
  ObArray<TableStorageLayouts::Definition> definitions;
  std::vector<InstanceNamespaceRecord> owners;
  pending_.reset();
  progress_.reset();
  const int64_t deadline = MIN(THIS_WORKER.get_timeout_ts(), ObTimeUtility::current_time() + 120L * 1000 * 1000);
  if (!initialized_ || access == nullptr || ls_service == nullptr) {
    ret = OB_NOT_INIT;
  } else if (stop_) {
    ret = OB_CANCELED;
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
  } else if (ls == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (ls->is_offline() || ls->get_ls_wrs_handler()->get_ls_weak_read_ts() < freeze_.frozen_scn_) {
    ret = OB_EAGAIN;
  } else if (!primary_) {
    // Standby retains the existing policy of physical verification. Its own
    // native incarnation/takeover/progress gate is enforced by the scheduler.
    progress_.total_table_cnt_ = 1;
    progress_.table_cnt_[ObTableCompactionInfo::VERIFIED] = 1;
    progress_.set_merge_finished();
  } else if (OB_FAIL(TableStorageLayouts::read_at(access->storage_schema_store(),
                 freeze_.frozen_scn_, deadline, definitions))) {
  } else {
    InstanceNamespaceDirectory directory(access->instance_meta_store());
    ret = directory.list_live(deadline, owners);
    bool waiting = false;
    for (const auto &owner : owners) {
      if (ret != OB_SUCCESS || stop_) { break; }
      bool participates = false;
      for (int64_t i = 0; !participates && i < definitions.count(); ++i) {
        participates = definitions.at(i).namespace_id == owner.id;
      }
      // No bindings at F: this owner was created later or owns no SQL objects.
      if (!participates) { continue; }
      ObMergeProgress owner_progress;
      const auto completed = completed_.find(owner.id);
      ns::NamespaceRuntime *runtime = nullptr;
      if (completed != completed_.end()) {
        owner_progress = completed->second;
      } else if (!ns::namespace_registry().get(owner.id, runtime) || runtime == nullptr) {
        ret = OB_EAGAIN;
      } else if (OB_FAIL(observer::namespace_worker_prototype::prepare_namespace_login(*runtime))) {
      } else {
        auto *sql = observer::namespace_worker_prototype::namespace_sql_proxy(owner.id);
        auto *schemas = observer::namespace_worker_prototype::namespace_schema_service(owner.id);
        if (sql == nullptr || schemas == nullptr) { ret = OB_NOT_INIT; }
        else {
          ret = ObChecksumValidator::check_namespace(owner.id, definitions, freeze_, *ls,
              *sql, *schemas, stop_, owner_progress, pending_);
        }
      }
      if (ret == OB_SUCCESS && completed == completed_.end()) {
        completed_.emplace(owner.id, owner_progress);
      }
      if (ret == OB_EAGAIN || ret == OB_SCHEMA_EAGAIN) {
        waiting = true;
        ret = OB_SUCCESS;
      }
      progress_.total_table_cnt_ += owner_progress.total_table_cnt_;
      progress_.merged_tablet_cnt_ += owner_progress.merged_tablet_cnt_;
      progress_.unmerged_tablet_cnt_ += owner_progress.unmerged_tablet_cnt_;
      for (int i = 0; i < ObMergeProgress::RECORD_TABLE_TYPE_CNT; ++i) {
        progress_.table_cnt_[i] += owner_progress.table_cnt_[i];
      }
    }
    if (ret == OB_SUCCESS && waiting) { ret = OB_EAGAIN; }
    if (ret == OB_SUCCESS && !stop_) { progress_.set_merge_finished(); }
  }
  if (stop_) { ret = OB_CANCELED; }
  if (ret != OB_SUCCESS) {
    LOG_INFO("Namespace historical merge verification pending", K(ret), K(freeze_.frozen_scn_), K(progress_));
  }
  // Missing inputs are ordinary progress, not a failed scheduler iteration.
  // Preserve merge_finish_=false and poll at the normal merge interval.
  if (ret == OB_EAGAIN || ret == OB_SCHEMA_EAGAIN) { ret = OB_SUCCESS; }
  return ret;
}
} // namespace rootserver
} // namespace oceanbase
