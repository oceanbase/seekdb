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

#include "storage/diff/ob_diff_tablet_scanner.h"
#include "storage/access/ob_sstable_row_whole_scanner.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/tablet/ob_tablet_obj_load_helper.h"
#include "storage/tablet/ob_tablet_table_store.h"
#include "storage/tablet/ob_tablet_table_store_iterator.h"
#include "storage/blocksstable/ob_sstable.h"
#include "storage/memtable/ob_memtable.h"
#include "storage/ob_storage_schema.h"
#include "storage/ob_i_table.h"
#include "storage/access/ob_table_read_info.h"
#include "share/scn.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace blocksstable;
using namespace share::schema;
namespace storage
{

ObDiffTabletScanner::ObDiffTabletScanner()
  : is_inited_(false),
    tenant_id_(OB_INVALID_TENANT_ID),
    ls_id_(),
    tablet_id_(),
    table_id_(OB_INVALID_ID),
    fork_snapshot_version_(0),
    read_snapshot_us_(0),
    alloc_(nullptr),
    local_alloc_("DiffTabletScan"),
    store_ctx_(),
    access_ctx_(),
    access_param_(),
    rowkey_read_info_(nullptr),
    storage_schema_(nullptr),
    rowkey_cnt_(0),
    output_col_cnt_(0),
    trans_idx_(OB_INVALID_INDEX),
    rows_(),
    cur_idx_(0)
{}

ObDiffTabletScanner::~ObDiffTabletScanner()
{
  reset();
}

void ObDiffTabletScanner::reset()
{
  if (nullptr != rowkey_read_info_) {
    rowkey_read_info_->~ObRowkeyReadInfo();
    rowkey_read_info_ = nullptr;
  }
  if (nullptr != storage_schema_) {
    storage_schema_->~ObStorageSchema();
    storage_schema_ = nullptr;
  }
  rows_.reset();
  local_alloc_.reset();
  is_inited_ = false;
}

int ObDiffTabletScanner::init(uint64_t tenant_id, share::ObLSID ls_id,
                              ObTabletID tablet_id, uint64_t table_id,
                              int64_t fork_snapshot_version,
                              int64_t read_snapshot_us,
                              ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else {
    tenant_id_ = tenant_id;
    ls_id_ = ls_id;
    tablet_id_ = tablet_id;
    table_id_ = table_id;
    fork_snapshot_version_ = fork_snapshot_version;
    read_snapshot_us_ = read_snapshot_us;
    alloc_ = &alloc;
    if (OB_FAIL(collect_all_())) {
      LOG_WARN("collect all failed", K(ret), K(ls_id), K(tablet_id));
    } else if (OB_FAIL(finalize_sort_())) {
      LOG_WARN("finalize_sort failed", K(ret));
    } else {
      cur_idx_ = 0;
      is_inited_ = true;
    }
  }
  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int ObDiffTabletScanner::get_next_row(const ObDiffMaterializedRow *&row)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (cur_idx_ >= rows_.count()) {
    ret = OB_ITER_END;
  } else {
    row = rows_.at(cur_idx_++);
  }
  return ret;
}

const blocksstable::ObStorageDatumUtils *ObDiffTabletScanner::get_datum_utils() const
{
  return rowkey_read_info_ != nullptr ? &rowkey_read_info_->get_datum_utils() : nullptr;
}

int ObDiffTabletScanner::collect_all_()
{
  int ret = OB_SUCCESS;
  ObLSService *ls_svc = nullptr;
  ObLSHandle ls_handle;
  ObTabletHandle tablet_handle;

  MTL_SWITCH(tenant_id_) {
    ls_svc = MTL(ObLSService *);
    if (OB_ISNULL(ls_svc)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ls_svc->get_ls(ls_id_, ls_handle, ObLSGetMod::TABLET_MOD))) {
      LOG_WARN("get ls failed", K(ret), K_(ls_id));
    } else if (OB_FAIL(ls_handle.get_ls()->get_tablet(tablet_id_, tablet_handle))) {
      LOG_WARN("get tablet failed", K(ret), K_(tablet_id));
    }

    // Load storage schema (multi-version aware) used to size rowkey_read_info_
    // and access_param_.
    if (OB_SUCC(ret)) {
      if (OB_FAIL(tablet_handle.get_obj()->load_storage_schema(local_alloc_, storage_schema_))) {
        LOG_WARN("load storage schema failed", K(ret));
      }
    }

    // Build rowkey read info (multi-version rowkey: includes trans_version
    // and sql_sequence pseudo-columns).
    int64_t full_stored_col_cnt = 0;
    ObSEArray<ObColDesc, 16> mv_cols_desc;
    if (OB_SUCC(ret)) {
      if (OB_FAIL(storage_schema_->get_mulit_version_rowkey_column_ids(mv_cols_desc))) {
        LOG_WARN("get mv rowkey col ids failed", K(ret));
      } else if (OB_FAIL(storage_schema_->get_store_column_count(full_stored_col_cnt, true))) {
        LOG_WARN("get store col count failed", K(ret));
      } else if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(local_alloc_, rowkey_read_info_))) {
        LOG_WARN("alloc rowkey read info failed", K(ret));
      } else if (OB_FAIL(rowkey_read_info_->init(local_alloc_,
                                                 full_stored_col_cnt,
                                                 storage_schema_->get_rowkey_column_num(),
                                                 storage_schema_->is_oracle_mode(),
                                                 mv_cols_desc,
                                                 false /*is_cg_sstable*/,
                                                 false /*use_default_compat_version*/,
                                                 false/*is_cs_replica_compat*/))) {
        LOG_WARN("init rowkey read info failed", K(ret));
      } else {
        rowkey_cnt_ = storage_schema_->get_rowkey_column_num();
        output_col_cnt_ = full_stored_col_cnt;
      }
    }

    // Build access_param_.
    if (OB_SUCC(ret)) {
      if (OB_FAIL(access_param_.init_merge_param(table_id_, tablet_id_, *rowkey_read_info_,
                                                  true /*is_multi_version_minor_merge*/,
                                                  false /*is_delete_insert*/))) {
        LOG_WARN("init access param failed", K(ret));
      } else {
        trans_idx_ = rowkey_read_info_->get_trans_col_index();
      }
    }

    // Build access_ctx_ at current snapshot.
    if (OB_SUCC(ret)) {
      ObQueryFlag qflag(ObQueryFlag::Forward,
                        false, /*daily merge*/
                        true,  /*use optimize*/
                        true,  /*whole macro scan*/
                        false, /*not full row*/
                        false, /*not index back*/
                        false  /*query stat*/);
      qflag.disable_cache();
      qflag.set_skip_running_tx(true);

      ObVersionRange vr;
      vr.snapshot_version_ = (read_snapshot_us_ > 0)
          ? read_snapshot_us_ : static_cast<int64_t>(OB_MAX_SCN_TS_NS);
      vr.multi_version_start_ = 1;
      vr.base_version_ = 0;

      SCN snap;
      if (read_snapshot_us_ > 0) {
        if (OB_FAIL(snap.convert_for_tx(read_snapshot_us_))) {
          LOG_WARN("convert snap failed", K(ret), K_(read_snapshot_us));
        }
      } else {
        snap = SCN::max_scn();
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(store_ctx_.init_for_read(ls_id_, tablet_id_,
                                                   INT64_MAX, -1, snap))) {
        LOG_WARN("store_ctx init failed", K(ret));
      } else if (OB_FAIL(access_ctx_.init(qflag, store_ctx_, local_alloc_, local_alloc_, vr))) {
        LOG_WARN("access_ctx init failed", K(ret));
      }
    }

    // Enumerate SSTables. Apply incremental two-tier filter:
    //   1) Major sstables are always skipped (they pre-date the fork).
    //   2) Minor sstables with max_merged_trans_version <= fork_snap
    //      are skipped wholesale — every row in them is older than the
    //      fork point and can't possibly contribute.
    //   3) Surviving sstables are scanned; the row-level filter in
    //      absorb_row_ drops any pre-fork rows that leaked in (e.g.
    //      sstables that span the fork point).
    int64_t n_total = 0, n_skip_major = 0, n_skip_minor = 0, n_scanned = 0;
    if (OB_SUCC(ret)) {
      ObTableStoreIterator ts_iter;
      if (OB_FAIL(tablet_handle.get_obj()->get_all_sstables(ts_iter))) {
        LOG_WARN("get sstables failed", K(ret));
      } else {
        ObITable *table = nullptr;
        while (OB_SUCC(ret)) {
          if (OB_FAIL(ts_iter.get_next(table))) {
            if (OB_ITER_END == ret) { ret = OB_SUCCESS; break; }
            else { break; }
          }
          if (OB_ISNULL(table)) continue;
          if (!table->is_sstable()) continue;
          if (table->is_mds_sstable()) continue;
          ++n_total;
          if (fork_snapshot_version_ > 0) {
            if (table->is_major_sstable()) {
              ++n_skip_major;
              LOG_INFO("[INCR SCAN] skip major sstable",
                       K_(tablet_id), K_(fork_snapshot_version),
                       "start_scn", table->get_start_scn().get_val_for_tx(),
                       "max_trans_version", table->get_max_merged_trans_version());
              continue;
            }
            if (table->get_max_merged_trans_version() <= fork_snapshot_version_) {
              ++n_skip_minor;
              LOG_INFO("[INCR SCAN] skip pre-fork minor sstable",
                       K_(tablet_id), K_(fork_snapshot_version),
                       "max_trans_version", table->get_max_merged_trans_version(),
                       "start_scn", table->get_start_scn().get_val_for_tx());
              continue;
            }
          }
          LOG_INFO("[INCR SCAN] scan sstable",
                   K_(tablet_id), K_(fork_snapshot_version),
                   "is_major", table->is_major_sstable(),
                   "start_scn", table->get_start_scn().get_val_for_tx(),
                   "max_trans_version", table->get_max_merged_trans_version());
          ++n_scanned;
          ObSSTable *sst = static_cast<ObSSTable *>(table);
          if (OB_FAIL(collect_from_sstable_(*sst))) {
            LOG_WARN("collect from sstable failed", K(ret));
          }
        }
      }
    }
    LOG_INFO("[INCR SCAN] sstable enumeration done",
             K_(tablet_id), K_(fork_snapshot_version),
             K(n_total), K(n_skip_major), K(n_skip_minor), K(n_scanned));

    // Enumerate memtables similarly.
    if (OB_SUCC(ret)) {
      ObArray<ObTableHandleV2> mt_handles;
      int tmp = tablet_handle.get_obj()->get_all_memtables_from_memtable_mgr(mt_handles);
      if (OB_ITER_END == tmp || OB_SUCCESS == tmp) {
        for (int64_t i = 0; OB_SUCC(ret) && i < mt_handles.count(); ++i) {
          ObITable *mt = mt_handles.at(i).get_table();
          if (OB_ISNULL(mt)) continue;
          if (!mt->is_data_memtable()) continue;
          // Do NOT filter memtables by start_scn for the delta path:
          // a memtable may have begun before the fork SCN yet still accept
          // post-fork commits. Row-level filtering happens via the access
          // context's version_range / trans_version_range. Skipping the
          // whole memtable would silently drop legitimate post-fork data.
          ObITabletMemtable *itmt = static_cast<ObITabletMemtable *>(mt);
          if (OB_FAIL(collect_from_memtable_(*itmt))) {
            LOG_WARN("collect from memtable failed", K(ret));
          }
        }
      }
    }
  }  // MTL_SWITCH
  return ret;
}

int ObDiffTabletScanner::collect_from_sstable_(ObSSTable &sstable)
{
  int ret = OB_SUCCESS;
  ObDatumRange whole;
  whole.set_whole_range();
  ObSSTableRowWholeScanner *iter = nullptr;
  void *buf = local_alloc_.alloc(sizeof(ObSSTableRowWholeScanner));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    iter = new (buf) ObSSTableRowWholeScanner();
    if (OB_FAIL(iter->init(access_param_.iter_param_, access_ctx_, &sstable, &whole))) {
      LOG_WARN("init sstable whole scanner failed", K(ret));
    } else {
      const ObDatumRow *row = nullptr;
      while (OB_SUCC(ret)) {
        if (OB_FAIL(iter->get_next_row(row))) {
          if (OB_ITER_END == ret) { ret = OB_SUCCESS; break; }
          else { break; }
        }
        if (OB_NOT_NULL(row)) {
          if (OB_FAIL(absorb_row_(*row))) {
            LOG_WARN("absorb row failed", K(ret));
          }
        }
      }
    }
    iter->~ObSSTableRowWholeScanner();
  }
  return ret;
}

int ObDiffTabletScanner::collect_from_memtable_(ObITabletMemtable &memtable)
{
  int ret = OB_SUCCESS;
  ObDatumRange whole;
  whole.set_whole_range();
  ObStoreRowIterator *iter = nullptr;
  if (OB_FAIL(memtable.scan(access_param_.iter_param_, access_ctx_, whole, iter))) {
    LOG_WARN("memtable scan failed", K(ret));
  } else if (OB_NOT_NULL(iter)) {
    const ObDatumRow *row = nullptr;
    while (OB_SUCC(ret)) {
      if (OB_FAIL(iter->get_next_row(row))) {
        if (OB_ITER_END == ret) { ret = OB_SUCCESS; break; }
        else { break; }
      }
      if (OB_NOT_NULL(row)) {
        if (OB_FAIL(absorb_row_(*row))) {
          LOG_WARN("absorb mt row failed", K(ret));
        }
      }
    }
    iter->~ObStoreRowIterator();
  }
  return ret;
}

// Take a raw multi-version row from a scanner, deep-copy into caller alloc,
// and append to rows_. Final dedup-by-PK happens in finalize_sort_.
int ObDiffTabletScanner::absorb_row_(const ObDatumRow &raw)
{
  int ret = OB_SUCCESS;
  // Extract trans_version up front so we can apply the row-level
  // pre-fork filter before any allocation.
  int64_t tv = 0;
  if (trans_idx_ != OB_INVALID_INDEX && trans_idx_ < raw.count_) {
    const ObStorageDatum &td = raw.storage_datums_[trans_idx_];
    if (!td.is_nop() && !td.is_null()) {
      tv = td.get_int();
      if (tv < 0) tv = -tv;
    }
  }
  if (fork_snapshot_version_ > 0 && tv > 0 && tv <= fork_snapshot_version_) {
    // Pre-fork row leaked through (sstable spans fork point); drop it.
    return OB_SUCCESS;
  }
  void *buf = alloc_->alloc(sizeof(ObDiffMaterializedRow));
  ObDiffMaterializedRow *m = nullptr;
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    m = new (buf) ObDiffMaterializedRow();
    m->is_delete_ = raw.row_flag_.is_delete();
    m->trans_version_ = tv;
    if (OB_FAIL(m->row_.init(*alloc_, raw.count_))) {
      LOG_WARN("init dst row failed", K(ret));
    } else {
      m->row_.row_flag_ = raw.row_flag_;
      m->row_.mvcc_row_flag_ = raw.mvcc_row_flag_;
      for (int64_t i = 0; OB_SUCC(ret) && i < raw.count_; ++i) {
        if (OB_FAIL(m->row_.storage_datums_[i].deep_copy(raw.storage_datums_[i], *alloc_))) {
          LOG_WARN("deep copy datum failed", K(ret), K(i));
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(rows_.push_back(m))) {
        LOG_WARN("push back row failed", K(ret));
      }
    }
  }
  return ret;
}

// Sort by PK ascending; among same-PK rows keep the one with the largest
// trans_version (which is the latest committed state at read SCN).
int ObDiffTabletScanner::finalize_sort_()
{
  int ret = OB_SUCCESS;
  const ObStorageDatumUtils *du = rowkey_read_info_ != nullptr ?
      &rowkey_read_info_->get_datum_utils() : nullptr;
  if (OB_ISNULL(du)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // Bubble sort is fine — typical diff candidate count is small.
    // For larger sets, replace with std::sort + custom comparator.
    const int64_t n = rows_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i + 1 < n; ++i) {
      for (int64_t j = 0; OB_SUCC(ret) && j + 1 < n - i; ++j) {
        ObDatumRowkey ka, kb;
        if (OB_FAIL(ka.assign(rows_.at(j)->row_.storage_datums_, rowkey_cnt_))) {
        } else if (OB_FAIL(kb.assign(rows_.at(j + 1)->row_.storage_datums_, rowkey_cnt_))) {
        } else {
          int cmp = 0;
          if (OB_FAIL(ka.compare(kb, *du, cmp))) {
          } else if (cmp > 0
                     || (cmp == 0 && rows_.at(j)->trans_version_ < rows_.at(j + 1)->trans_version_)) {
            ObDiffMaterializedRow *tmp = rows_.at(j);
            rows_.at(j) = rows_.at(j + 1);
            rows_.at(j + 1) = tmp;
          }
        }
      }
    }
    // Dedup adjacent same-PK rows, keep first (= largest trans_version).
    int64_t w = 0;
    for (int64_t r = 0; OB_SUCC(ret) && r < n; ++r) {
      if (w == 0) {
        rows_.at(w++) = rows_.at(r);
      } else {
        ObDatumRowkey ka, kb;
        if (OB_FAIL(ka.assign(rows_.at(w - 1)->row_.storage_datums_, rowkey_cnt_))) {
        } else if (OB_FAIL(kb.assign(rows_.at(r)->row_.storage_datums_, rowkey_cnt_))) {
        } else {
          int cmp = 0;
          if (OB_FAIL(ka.compare(kb, *du, cmp))) {
          } else if (cmp == 0) {
            // duplicate PK, skip (older version)
          } else {
            rows_.at(w++) = rows_.at(r);
          }
        }
      }
    }
    if (OB_SUCC(ret) && w < n) {
      while (rows_.count() > w) {
        rows_.pop_back();
      }
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
