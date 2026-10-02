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

namespace oceanbase {
namespace ns {
using namespace common;
using namespace share::schema;
using storage::NamespaceForkKernelPrototype;

int TabletBinding::prepare()
{
  int ret = OB_SUCCESS;
  if (!positions_.empty()) { return ret; }
  schemas_.reset();
  tablets_.reset();
  const ObTableSchema *main = requested_;
  if (requested_->is_aux_lob_table()) {
    if (OB_FAIL(guard_.get_table_schema(requested_->get_data_table_id(), main))) {
    } else if (main == nullptr || main->is_aux_lob_table()) {
      ret = OB_SCHEMA_EAGAIN;
    }
  }
  if (OB_SUCC(ret)) { ret = schemas_.push_back(main); }
  const uint64_t auxiliary_ids[] = {
      OB_SUCC(ret) ? main->get_aux_lob_meta_tid() : OB_INVALID_ID,
      OB_SUCC(ret) ? main->get_aux_lob_piece_tid() : OB_INVALID_ID};
  for (uint64_t id : auxiliary_ids) {
    if (OB_SUCC(ret) && id != 0 && id != OB_INVALID_ID) {
      const ObTableSchema *auxiliary = nullptr;
      if (id == requested_->get_table_id()) { auxiliary = requested_; }
      else { ret = guard_.get_table_schema(id, auxiliary); }
      if (OB_FAIL(ret)) {
      } else if (auxiliary == nullptr || !auxiliary->is_aux_lob_table()
          || auxiliary->get_data_table_id() != main->get_table_id()) {
        ret = OB_SCHEMA_EAGAIN;
      } else { ret = schemas_.push_back(auxiliary); }
    }
  }
  bool found = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < schemas_.count(); ++i) {
    ObArray<ObTabletID> ids;
    const auto &schema = *schemas_.at(i);
    if (OB_FAIL(schema.get_tablet_ids(ids))) {
    } else if (schema.get_hidden_partition_num() > 0
        && OB_FAIL(schema.get_first_level_hidden_tablet_ids(ids))) {
    } else if (ids.empty() || (i > 0 && ids.count() != tablets_.at(0).count())) {
      ret = OB_STATE_NOT_MATCH;
    } else if (OB_FAIL(tablets_.push_back(ids))) {
    } else if (schema.get_table_id() == requested_->get_table_id()) {
      found = true;
      for (int64_t j = 0; OB_SUCC(ret) && j < ids.count(); ++j) {
        if (!positions_.emplace(ids.at(j).id(), j).second) { ret = OB_STATE_NOT_MATCH; }
      }
    }
  }
  if (OB_SUCC(ret) && !found) { ret = OB_SCHEMA_EAGAIN; }
  if (OB_FAIL(ret)) { positions_.clear(); }
  return ret;
}

int TabletBinding::resolve(const ObTabletID &logical_tablet,
    ObIArray<const ObTableSchema *> &schemas, ObIArray<ObTabletID> &tablets)
{
  int ret = prepare();
  if (OB_SUCC(ret)) {
    const auto found = positions_.find(logical_tablet.id());
    if (found == positions_.end()) { ret = OB_TABLET_NOT_EXIST; }
    else if (OB_FAIL(schemas.assign(schemas_))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < tablets_.count(); ++i) {
        ret = tablets.push_back(tablets_.at(i).at(found->second));
      }
    }
  }
  return ret;
}

void TabletAccess::reset()
{
  protection_.reset();
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

int TabletAccess::prepare_read(uint64_t namespace_id, uint64_t table_id,
                               const ObTabletID &logical_tablet,
                               data_plane::ObNamespaceAccessMode mode)
{
  int ret = route(namespace_id, logical_tablet);
  if (OB_SUCC(ret)) {
    ret = NamespaceForkKernelPrototype::prepare_access(namespace_id, table_id,
        tablet_, true, mode, protection_, [&](ObTabletID &physical) {
      return NamespaceForkKernelPrototype::resolve_read_tablet(
          schema_tablet_, physical, cap_scn_);
    });
  }
  return ret;
}

int TabletAccess::prepare_write(uint64_t namespace_id, uint64_t table_id,
                                const ObTabletID &logical_tablet,
                                data_plane::ObNamespaceAccessMode mode,
                                const PrepareBinding &prepare)
{
  int ret = route(namespace_id, logical_tablet);
  if (OB_SUCC(ret)) {
    ret = NamespaceForkKernelPrototype::prepare_access(namespace_id, table_id,
        tablet_, false, mode, protection_, [&](ObTabletID &physical) {
      return NamespaceForkKernelPrototype::ensure_tablet(physical, prepare);
    });
  }
  return ret;
}

int TabletAccess::prepare_scan(uint64_t namespace_id,
                               data_plane::ObNamespaceAccessMode mode,
                               storage::ObTableScanParam &param)
{
  int ret = prepare_read(namespace_id, param.index_id_, param.tablet_id_, mode);
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
