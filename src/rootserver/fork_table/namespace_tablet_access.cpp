/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "rootserver/fork_table/namespace_tablet_access.h"
#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "data_plane/access/ob_table_scan_param.h"
#include "data_plane/transaction/ob_i_transaction_service.h"
#include "observer/namespace_worker_protocol_prototype.h"
#include "lib/time/ob_time_utility.h"

namespace oceanbase {
namespace ns {
using namespace common;
using namespace share::schema;
using storage::NamespaceForkKernelPrototype;

void TabletAccess::reset()
{
  protection_.reset();
  view_.reset();
  tablet_.reset();
  schema_tablet_.reset();
  cap_scn_ = 0;
}

int TabletAccess::prepare_lob_read(uint64_t namespace_id, const ObLobLocatorV2 &locator,
                                  data_plane::ObNamespaceAccessMode mode)
{
  int ret = OB_SUCCESS;
  if (!locator.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (locator.has_lob_header() && locator.is_persist_lob()
      && !locator.has_inrow_data()) {
    ObMemLobLocationInfo *location = nullptr;
    if (OB_FAIL(locator.get_location_info(location))) {
    } else if (location == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ObTabletID source(location->tablet_id_);
      ret = NamespaceForkKernelPrototype::prepare_access(namespace_id, OB_INVALID_ID,
          source, true, mode, protection_, [](ObTabletID &) { return OB_SUCCESS; });
    }
  }
  return ret;
}

int TabletAccess::route(uint64_t namespace_id, const ObTabletID &logical_tablet)
{
  uint64_t physical = OB_INVALID_ID;
  const int ret = NamespaceForkKernelPrototype::storage_object_id(
      namespace_id, logical_tablet.id(), physical);
  if (ret == OB_SUCCESS) {
    schema_tablet_ = ObTabletID(physical);
    tablet_ = schema_tablet_;
    cap_scn_ = 0;
  }
  return ret;
}

int TabletAccess::prepare_current_read(uint64_t namespace_id, uint64_t table_id,
    const ObTabletID &logical_tablet, data_plane::ObNamespaceAccessMode mode)
{
  auto *lifecycle = observer::namespace_worker_prototype::namespace_schema_lifecycle(namespace_id);
  auto *transactions = data_plane::query_transaction_service();
  if (lifecycle == nullptr || transactions == nullptr) { return OB_NOT_INIT; }
  NamespaceCatalogViews::Handle view;
  int ret = lifecycle->acquire_read_view([&](share::SCN &snapshot) {
    return transactions->get_read_snapshot_version(
        ObTimeUtility::current_time() + 1000000, snapshot);
  }, view, {});
  if (OB_SUCC(ret)) { ret = prepare_read(namespace_id, table_id, logical_tablet, mode, view); }
  return ret;
}

int TabletAccess::prepare_read(uint64_t namespace_id, uint64_t table_id,
                               const ObTabletID &logical_tablet,
                               data_plane::ObNamespaceAccessMode mode,
                               const NamespaceCatalogViews::Handle &view)
{
  int ret = route(namespace_id, logical_tablet);
  if (OB_SUCC(ret)) {
    view_ = view;
    ret = NamespaceForkKernelPrototype::prepare_access(namespace_id, table_id,
        tablet_, true, mode, protection_, [&](ObTabletID &physical) {
      return NamespaceForkKernelPrototype::resolve_read_tablet(
          schema_tablet_, physical, cap_scn_, view_);
    });
  }
  return ret;
}

int TabletAccess::prepare_write(uint64_t namespace_id, uint64_t table_id,
                                const ObTabletID &logical_tablet,
                                data_plane::ObNamespaceAccessMode mode)
{
  int ret = route(namespace_id, logical_tablet);
  if (OB_SUCC(ret)) {
    ret = NamespaceForkKernelPrototype::prepare_access(namespace_id, table_id,
        tablet_, false, mode, protection_, [&](ObTabletID &physical) {
      return NamespaceForkKernelPrototype::ensure_tablet(physical);
    });
  }
  return ret;
}

int TabletAccess::prepare_scan(uint64_t namespace_id,
                               data_plane::ObNamespaceAccessMode mode,
                               storage::ObTableScanParam &param,
                               const NamespaceCatalogViews::Handle &view)
{
  int ret = prepare_read(namespace_id, param.index_id_, param.tablet_id_, mode, view);
  if (OB_SUCC(ret)) {
    param.tablet_id_ = tablet_;
    param.schema_tablet_id_ = schema_tablet_;
    if (cap_scn_ > 0) {
      share::SCN cap;
      if (OB_FAIL(cap.convert_for_tx(cap_scn_))) {
      } else if (!param.fb_snapshot_.is_valid() || cap < param.fb_snapshot_) {
        param.fb_snapshot_ = cap;
      }
    }
  }
  return ret;
}
} // namespace ns
} // namespace oceanbase
