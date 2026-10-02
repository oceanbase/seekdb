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

int TabletBinding::init(uint64_t namespace_id, const ObTableSchema &requested,
                        ObSchemaGetterGuard &guard)
{
  int ret = OB_SUCCESS;
  if (!definitions_.empty()) { return OB_INIT_TWICE; }
  const ObTableSchema *main = &requested;
  if (requested.is_aux_lob_table()) {
    if (OB_FAIL(guard.get_table_schema(requested.get_data_table_id(), main))) {
    } else if (main == nullptr || main->is_aux_lob_table()) {
      ret = OB_SCHEMA_EAGAIN;
    }
  }
  ObSEArray<const ObTableSchema *, 3> logical;
  if (OB_SUCC(ret)) { ret = logical.push_back(main); }
  const uint64_t auxiliary_ids[] = {
      OB_SUCC(ret) ? main->get_aux_lob_meta_tid() : OB_INVALID_ID,
      OB_SUCC(ret) ? main->get_aux_lob_piece_tid() : OB_INVALID_ID};
  for (uint64_t id : auxiliary_ids) {
    if (OB_SUCC(ret) && id != 0 && id != OB_INVALID_ID) {
      const ObTableSchema *auxiliary = nullptr;
      if (id == requested.get_table_id()) { auxiliary = &requested; }
      else { ret = guard.get_table_schema(id, auxiliary); }
      if (OB_FAIL(ret)) {
      } else if (auxiliary == nullptr || !auxiliary->is_aux_lob_table()
          || auxiliary->get_data_table_id() != main->get_table_id()) {
        ret = OB_SCHEMA_EAGAIN;
      } else {
        ret = logical.push_back(auxiliary);
      }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < logical.count(); ++i) {
    auto physical = std::make_unique<ObTableSchema>(&allocator_);
    if (OB_FAIL(NamespaceForkKernelPrototype::make_storage_schema(
            namespace_id, *logical.at(i), *physical))) {
    } else if (OB_FAIL(schemas_.push_back(physical.get()))) {
    } else {
      if (logical.at(i)->get_table_id() == requested.get_table_id()) {
        requested_ = physical.get();
      }
      definitions_.push_back(std::move(physical));
    }
  }
  if (OB_SUCC(ret) && requested_ == nullptr) { ret = OB_SCHEMA_EAGAIN; }
  return ret;
}

int TabletBinding::ensure(const ObTabletID &storage_tablet) const
{
  return requested_ == nullptr ? OB_NOT_INIT
      : NamespaceForkKernelPrototype::ensure_tablet(storage_tablet, *requested_, schemas_);
}

void TabletAccess::reset()
{
  NamespaceForkKernelPrototype::release_access(held_);
  tablet_.reset();
  schema_tablet_.reset();
  cap_scn_ = 0;
}

int TabletAccess::prepare_lob_read(const ObLobLocatorV2 &locator,
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
      ret = NamespaceForkKernelPrototype::check_table_access(OB_INVALID_ID,
          ObTabletID(location->tablet_id_), true, mode, held_);
    }
  }
  return ret;
}

int TabletAccess::admit(uint64_t namespace_id, uint64_t table_id,
                        const ObTabletID &logical_tablet, bool read_only,
                        data_plane::ObNamespaceAccessMode mode)
{
  int ret = OB_SUCCESS;
  uint64_t physical = OB_INVALID_ID;
  if (OB_FAIL(NamespaceForkKernelPrototype::storage_object_id(
          namespace_id, logical_tablet.id(), physical))) {
  } else {
    schema_tablet_ = ObTabletID(physical);
    tablet_ = schema_tablet_;
    cap_scn_ = 0;
    // Reusing a write context must not drop its existing access protection
    // between batches. The kernel registers an already-held lease only once.
    ret = NamespaceForkKernelPrototype::check_table_access(
        table_id, schema_tablet_, read_only, mode, held_);
  }
  return ret;
}

int TabletAccess::prepare_read(uint64_t namespace_id, uint64_t table_id,
                               const ObTabletID &logical_tablet,
                               data_plane::ObNamespaceAccessMode mode)
{
  int ret = admit(namespace_id, table_id, logical_tablet, true, mode);
  if (OB_SUCC(ret)) {
    ret = NamespaceForkKernelPrototype::resolve_read_tablet(
        schema_tablet_, tablet_, cap_scn_);
  }
  return ret;
}

int TabletAccess::prepare_write(uint64_t namespace_id, uint64_t table_id,
                                const ObTabletID &logical_tablet,
                                data_plane::ObNamespaceAccessMode mode,
                                const TabletBinding &binding)
{
  int ret = admit(namespace_id, table_id, logical_tablet, false, mode);
  if (OB_SUCC(ret)) { ret = binding.ensure(tablet_); }
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
