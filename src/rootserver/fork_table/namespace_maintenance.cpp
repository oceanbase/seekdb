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
#include "rootserver/fork_table/namespace_maintenance.h"
#include "rootserver/fork_table/instance_namespace_metadata.h"
#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "share/ob_shared_timer.h"
#include "share/rc/ob_server_runtime.h"
#include "share/ob_server_struct.h"
#include <unordered_set>

namespace oceanbase {
namespace rootserver {
using namespace common;

int NamespaceMaintenance::start(share::ObISharedTimer &timer, storage::InstanceMetaStore &store)
{
  if (timer_ != nullptr) { return OB_INIT_TWICE; }
  store_ = &store;
  const int ret = timer.schedule(*this, 5 * 1000 * 1000L, true);
  if (ret == OB_SUCCESS) { timer_ = &timer; }
  else { store_ = nullptr; }
  return ret;
}

void NamespaceMaintenance::stop()
{
  if (timer_ != nullptr) {
    timer_->cancel_task(*this);
    timer_->wait_task(*this);
    timer_ = nullptr;
  }
  store_ = nullptr;
  positions_.clear();
  last_namespace_ = physical_cursor_ = 0;
}

void NamespaceMaintenance::runTimerTask()
{
  if (!ATOMIC_LOAD(&GCTX.sys_package_ready_) || !share::server_is_write_enabled()) { return; }
  const int64_t previous_timeout = THIS_WORKER.get_timeout_ts();
  THIS_WORKER.set_timeout_ts(ObTimeUtility::current_time() + 30 * 1000 * 1000L);
  int ret = storage::NamespaceForkKernelPrototype::collect_dropped_namespace_tablets(physical_cursor_);
  if (ret != OB_SUCCESS) { LOG_WARN("namespace physical cleanup failed", K(ret)); }
  ret = materialize_inherited_tablets();
  if (ret != OB_SUCCESS) { LOG_WARN("namespace background materialization failed", K(ret)); }
  InstanceNamespaceDirectory directory(*store_);
  int64_t deleted = 0;
  ret = directory.collect_catalog_pages(THIS_WORKER.get_timeout_ts(), deleted);
  if (ret != OB_SUCCESS && ret != OB_EAGAIN) { LOG_WARN("namespace directory page collection failed", K(ret)); }
  THIS_WORKER.set_timeout_ts(previous_timeout);
}

int NamespaceMaintenance::materialize_inherited_tablets()
{
  const int64_t previous_timeout = THIS_WORKER.get_timeout_ts();
  const int64_t deadline = std::min(THIS_WORKER.get_timeout_ts(),
      ObTimeUtility::current_time() + 2 * 1000 * 1000L);
  THIS_WORKER.set_timeout_ts(deadline);
  rootserver::InstanceNamespaceDirectory directory(*store_);
  std::vector<rootserver::InstanceNamespaceRecord> live;
  int ret = directory.list_live(deadline, live);
  uint64_t selected = 0, first = 0;
  std::unordered_set<uint64_t> ids;
  if (OB_SUCC(ret)) {
    for (const auto &record : live) {
      if (record.parent_namespace == 0) { continue; }
      ids.insert(record.id);
      if (first == 0 || record.id < first) { first = record.id; }
      if (record.id > last_namespace_ && (selected == 0 || record.id < selected)) {
        selected = record.id;
      }
    }
    for (auto it = positions_.begin(); it != positions_.end();) {
      if (ids.count(it->first) == 0) { it = positions_.erase(it); }
      else { ++it; }
    }
    if (selected == 0) { selected = first; }
  }
  int64_t materialized = 0;
  if (OB_SUCC(ret) && selected != 0) {
    last_namespace_ = selected;
    auto &position = positions_[selected];
    std::vector<std::pair<std::string, ns::CatalogValue>> entries;
    ret = directory.scan_sources(selected, position, deadline, entries);
    if (OB_SUCC(ret)) {
      size_t consumed = 0;
      for (const auto &entry : entries) {
        if (materialized >= 16 || ObTimeUtility::current_time() >= deadline) { break; }
        position = entry.first;
        ++consumed;
        ns::CatalogTabletSource source;
        if (!ns::NamespaceCatalogCodec::decode_source(entry.second.data, source)) {
          ret = OB_CHECKSUM_ERROR;
          break;
        }
        // A main tablet and its LOB tablets form one creation transaction.
        // Visiting the main entry suffices; indexes have their own binding unit.
        if (entry.first != ns::NamespaceCatalogCodec::object_key(source.data_tablet_id)
            || source.physical_tablet_id == ns::NamespaceObjectKey{selected, source.data_tablet_id}.storage_id()) { continue; }
        const int rc = storage::NamespaceForkKernelPrototype::materialize_source(selected, source.table_id, source.data_tablet_id);
        ++materialized;
        if (rc != OB_SUCCESS && rc != OB_TABLET_NOT_EXIST
            && rc != OB_ENTRY_NOT_EXIST && rc != OB_OP_NOT_ALLOW) {
          ret = rc;
          break;
        }
      }
      if (consumed == entries.size() && entries.size() < 64) { position.clear(); }
    }
  }
  THIS_WORKER.set_timeout_ts(previous_timeout);
  if (materialized != 0 || OB_FAIL(ret)) {
    LOG_INFO("namespace background materialization", K(ret), K(selected), K(materialized));
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
