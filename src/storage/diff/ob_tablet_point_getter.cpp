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

#define USING_LOG_PREFIX STORAGE

#include "storage/diff/ob_tablet_point_getter.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ls/ob_ls.h"
#include "storage/tablet/ob_tablet.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_multi_version_schema_service.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace blocksstable;
namespace storage
{

ObTabletPointGetter::ObTabletPointGetter()
  : is_inited_(false),
    tenant_id_(OB_INVALID_TENANT_ID),
    ls_id_(),
    tablet_id_(),
    table_id_(OB_INVALID_ID),
    allocator_("DiffPGAlloc"),
    stmt_allocator_("DiffPGStmt"),
    tablet_handle_(),
    schema_param_(allocator_),
    relative_table_(),
    access_param_(),
    store_ctx_(),
    access_ctx_(),
    get_table_param_(),
    single_merge_(),
    out_cols_project_(),
    store_col_count_(0),
    rowkey_col_count_(0)
{
}

ObTabletPointGetter::~ObTabletPointGetter()
{
  reset();
}

void ObTabletPointGetter::reset()
{
  if (is_inited_) {
    single_merge_.reset();
  }
  get_table_param_.reset();
  access_ctx_.reset();
  access_param_.reset();
  relative_table_.destroy();
  schema_param_.reset();
  tablet_handle_.reset();
  out_cols_project_.reset();
  stmt_allocator_.reset();
  allocator_.reset();
  is_inited_ = false;
}

int ObTabletPointGetter::init(uint64_t tenant_id,
                              share::ObLSID ls_id,
                              ObTabletID tablet_id,
                              uint64_t table_id,
                              share::SCN read_snapshot)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else {
    tenant_id_ = tenant_id;
    ls_id_ = ls_id;
    tablet_id_ = tablet_id;
    table_id_ = table_id;
  }

  // Acquire tablet + schema param + relative table. We need a real tenant
  // context because schema_guard requires it.
  ObLSService *ls_svc = nullptr;
  ObLSHandle ls_handle;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;
  if (OB_FAIL(ret)) {
    // skip
  } else if (FALSE_IT(ls_svc = MTL(ObLSService *))) {
  } else if (OB_ISNULL(ls_svc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls svc null", K(ret));
  } else if (OB_FAIL(ls_svc->get_ls(ls_id_, ls_handle, ObLSGetMod::TABLET_MOD))) {
    LOG_WARN("get ls failed", K(ret), K_(ls_id));
  } else if (OB_FAIL(ls_handle.get_ls()->get_tablet(tablet_id_, tablet_handle_))) {
    LOG_WARN("get tablet failed", K(ret), K_(tablet_id));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
                 tenant_id_, schema_guard))) {
    LOG_WARN("get tenant schema guard failed", K(ret), K_(tenant_id));
  } else if (OB_FAIL(schema_guard.get_table_schema(tenant_id_, table_id_, table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K_(tenant_id), K_(table_id));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
  } else if (OB_FAIL(schema_param_.convert(table_schema))) {
    LOG_WARN("schema param convert failed", K(ret));
  } else if (OB_FAIL(relative_table_.init(&schema_param_, tablet_id_))) {
    LOG_WARN("init relative table failed", K(ret));
  } else if (OB_FAIL(relative_table_.tablet_iter_.set_tablet_handle(tablet_handle_))) {
    LOG_WARN("set tablet handle on relative_table failed", K(ret));
  } else if (OB_FAIL(table_schema->get_store_column_count(store_col_count_))) {
    LOG_WARN("get store col count failed", K(ret));
  } else {
    rowkey_col_count_ = table_schema->get_rowkey_column_num();
  }

  // Project all stored, non-virtual columns. Matches the schema_param_ order
  // used by init_dml_access_param.
  if (OB_SUCC(ret)) {
    out_cols_project_.set_tenant_id(MTL_ID());
    for (int64_t i = 0; OB_SUCC(ret) && i < schema_param_.get_columns().count(); ++i) {
      if (schema_param_.get_columns().at(i)->is_virtual_gen_col()) {
        // skip
      } else if (OB_FAIL(out_cols_project_.push_back(static_cast<int32_t>(i)))) {
        LOG_WARN("push col idx failed", K(ret), K(i));
      }
    }
  }

  // Access param — DML init path is what point-get callers (e.g. unique index
  // conflict scan) use, exactly what we want here.
  if (OB_SUCC(ret)) {
    if (OB_FAIL(access_param_.init_dml_access_param(
            relative_table_,
            tablet_handle_.get_obj()->get_rowkey_read_info(),
            schema_param_,
            &out_cols_project_))) {
      LOG_WARN("init dml access param failed", K(ret));
    } else {
      // init_dml_access_param relies on relative_table_.tablet_iter_'s tablet
      // being non-null to populate ls_id; ensure it ends up correct for the
      // memtable-aware merge path which validates ls_id.
      access_param_.iter_param_.ls_id_ = ls_id_;
    }
  }

  // Store ctx + access ctx. We use a max-SCN snapshot (or explicit one if
  // caller asked) and a normal (non multi-version-minor-merge) query flag so
  // ObSingleMerge runs its fuse-row path. Tombstones in the source data show
  // up as row_flag.is_not_exist() at get_next_row.
  if (OB_SUCC(ret)) {
    ObQueryFlag flag(ObQueryFlag::Forward,
                     false, /*daily merge*/
                     true,  /*use optimize*/
                     false, /*whole macro scan*/
                     false, /*not full row*/
                     false, /*not index back*/
                     false  /*query stat*/);
    flag.disable_cache();
    flag.set_skip_running_tx(true);

    share::SCN snap = read_snapshot.is_valid() ? read_snapshot : share::SCN::max_scn();
    ObVersionRange vr;
    vr.snapshot_version_ = snap.is_max() ? static_cast<int64_t>(OB_MAX_SCN_TS_NS)
                                          : snap.get_val_for_tx();
    vr.multi_version_start_ = 0;
    vr.base_version_ = 0;
    if (OB_FAIL(store_ctx_.init_for_read(ls_id_, tablet_id_, INT64_MAX, -1, snap))) {
      LOG_WARN("init store ctx failed", K(ret));
    } else if (OB_FAIL(access_ctx_.init(flag, store_ctx_, allocator_, stmt_allocator_, vr))) {
      LOG_WARN("init access ctx failed", K(ret));
    }
  }

  // Get-table param: copy the tablet handle and refresh read tables so
  // ObSingleMerge sees every visible SSTable + memtable on this tablet.
  if (OB_SUCC(ret)) {
    if (OB_FAIL(get_table_param_.tablet_iter_.set_tablet_handle(tablet_handle_))) {
      LOG_WARN("set tablet handle failed", K(ret));
    } else if (OB_FAIL(get_table_param_.tablet_iter_.refresh_read_tables_from_tablet(
                   INT64_MAX,
                   false /*allow_not_ready*/,
                   false /*major_sstable_only*/,
                   false /*need_split_src_table*/,
                   false /*need_split_dst_table*/))) {
      LOG_WARN("refresh read tables failed", K(ret));
    } else if (OB_FAIL(single_merge_.init(access_param_, access_ctx_, get_table_param_))) {
      LOG_WARN("init single merge failed", K(ret));
    } else {
      is_inited_ = true;
    }
  }

  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int ObTabletPointGetter::get(const ObDatumRowkey &pk, const ObDatumRow *&row_out)
{
  int ret = OB_SUCCESS;
  row_out = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!pk.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    single_merge_.reuse();
    allocator_.reuse();
    if (OB_FAIL(single_merge_.open(pk))) {
      LOG_WARN("open single merge failed", K(ret), K(pk));
    } else {
      ObDatumRow *row = nullptr;
      if (OB_FAIL(single_merge_.get_next_row(row))) {
        if (OB_ITER_END == ret) {
          ret = OB_ENTRY_NOT_EXIST;
        } else {
          LOG_WARN("single merge get next failed", K(ret), K(pk));
        }
      } else if (OB_ISNULL(row)) {
        ret = OB_ENTRY_NOT_EXIST;
      } else if (row->row_flag_.is_not_exist() || row->row_flag_.is_delete()) {
        ret = OB_ENTRY_NOT_EXIST;
      } else {
        row_out = row;
      }
    }
  }
  return ret;
}

int64_t ObTabletPointGetter::get_col_pos(uint64_t col_id) const
{
  int64_t pos = -1;
  if (is_inited_) {
    int64_t cur = 0;
    for (int64_t i = 0; i < schema_param_.get_columns().count(); ++i) {
      const ObColumnParam *col = schema_param_.get_columns().at(i);
      if (OB_ISNULL(col) || col->is_virtual_gen_col()) continue;
      if (col->get_column_id() == col_id) {
        pos = cur;
        break;
      }
      ++cur;
    }
  }
  return pos;
}

} // namespace storage
} // namespace oceanbase
