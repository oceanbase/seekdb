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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_diff_table_op_compute.h"
#include "sql/resolver/cmd/ob_diff_table_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_column_schema.h"
#include "share/ob_fork_table_util.h"
#include "common/row/ob_row.h"
#include "storage/diff/ob_diff_tablet_scanner.h"
#include "storage/diff/ob_tablet_point_getter.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ls/ob_ls.h"
#include "storage/tablet/ob_tablet.h"
#include "share/ob_ls_id.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace storage;
namespace sql
{

namespace {

enum class DiffSide { CURRENT_SIDE, INCOMING_SIDE };

// NULL-safe equal between two ObStorageDatums (`<=>`).
bool nullsafe_eq_(const blocksstable::ObStorageDatum &a,
                  const blocksstable::ObStorageDatum &b)
{
  if (a.is_null() && b.is_null()) return true;
  if (a.is_null() || b.is_null()) return false;
  if (a.is_nop() && b.is_nop()) return true;
  if (a.is_nop() || b.is_nop()) return false;
  if (a.len_ != b.len_) return false;
  return MEMCMP(a.ptr_, b.ptr_, a.len_) == 0;
}

// Build a storage rowkey from the first rowkey_cnt datums of a row.
// Also populates store_rowkey_ via prepare_memtable_readable so the rowkey
// is acceptable to the memtable get path used by ObSingleMerge.
int make_rowkey_(const blocksstable::ObDatumRow &row, int64_t rowkey_cnt,
                 const ObIArray<ObColDesc> &rowkey_col_descs,
                 ObIAllocator &alloc,
                 blocksstable::ObDatumRowkey &rk)
{
  int ret = rk.assign(row.storage_datums_, rowkey_cnt);
  if (OB_SUCC(ret)) {
    ret = rk.prepare_memtable_readable(rowkey_col_descs, alloc);
  }
  return ret;
}

// Compare two ObDatumRowkeys via storage datum utils — semantically correct
// (handles types, collation, nulls) unlike a raw byte compare.
int rowkey_cmp_(const blocksstable::ObDatumRowkey &a,
                const blocksstable::ObDatumRowkey &b,
                const blocksstable::ObStorageDatumUtils &du,
                int &cmp)
{
  return a.compare(b, du, cmp);
}

// Map: output col index -> position in storage row layout.
// pk_pos_in_storage[i]   = where pk_cols[i] lives in storage-order schema
// val_pos_in_storage[i]  = where val_cols[i] lives in storage-order schema
// These positions are *schema-storage-order*; they index a point-getter
// row directly, and a multi-version row with a +2 shift past the rowkey.
struct DiffColMap
{
  ObSEArray<int64_t, 8> pk_pos_;       // schema-storage position; valid for point-getter rows
  ObSEArray<int64_t, 16> val_pos_;
  ObSEArray<int64_t, 8> delta_pk_pos_; // multi-version layout position; valid for delta-scanner rows
  ObSEArray<int64_t, 16> delta_val_pos_;
  int64_t rowkey_cnt_;   // = storage rowkey column count
  // Storage rowkey column descs (in storage order, length = rowkey_cnt_).
  // Needed to pack a memtable-readable store_rowkey on candidate PKs.
  ObSEArray<ObColDesc, 8> rk_col_descs_;
  // Datum compare utils for the USER PK columns (in order of stmt.pk_cols()).
  // Used by fallback to compare rows by user PK regardless of storage rowkey
  // layout (matters for heap tables where storage rowkey is hidden_pk).
  const ObColDescIArray *user_pk_col_descs_ = nullptr;
};

int build_col_map_(const ObTableSchema &schema,
                   const ObDiffTableStmt &stmt,
                   DiffColMap &m)
{
  int ret = OB_SUCCESS;
  m.rowkey_cnt_ = schema.get_rowkey_column_num();
  // Storage-ordered, non-virtual column list. The point-getter row layout
  // matches this exact filtering+ordering.
  ObSEArray<ObColDesc, 32> col_descs;
  if (OB_FAIL(schema.get_column_ids(col_descs, true /*no_virtual*/))) {
    LOG_WARN("get column ids failed", K(ret));
  } else {
    // First rowkey_cnt_ entries of get_column_ids(no_virtual=true) are the
    // storage rowkey columns in order.
    for (int64_t i = 0; OB_SUCC(ret) && i < m.rowkey_cnt_ && i < col_descs.count(); ++i) {
      if (OB_FAIL(m.rk_col_descs_.push_back(col_descs.at(i)))) break;
    }
  }
  auto find_pos = [&](const ObString &name, int64_t &out) -> int {
    int r = OB_SUCCESS;
    out = -1;
    const ObColumnSchemaV2 *c = schema.get_column_schema(name);
    if (OB_ISNULL(c)) {
      r = OB_ERR_UNEXPECTED;
    } else {
      const uint64_t cid = c->get_column_id();
      for (int64_t i = 0; i < col_descs.count(); ++i) {
        if (col_descs.at(i).col_id_ == cid) { out = i; break; }
      }
    }
    return r;
  };
  for (int64_t i = 0; OB_SUCC(ret) && i < stmt.pk_cols().count(); ++i) {
    int64_t pos = -1;
    if (OB_FAIL(find_pos(stmt.pk_cols().at(i), pos))) {
    } else if (OB_FAIL(m.pk_pos_.push_back(pos))) {
    } else {
      // delta-row layout: rowkey at [0..rowkey_cnt), pseudo cols at
      // [rowkey_cnt, rowkey_cnt+1], non-rowkey stored cols at [rowkey_cnt+2..]
      const int64_t dpos = (pos < m.rowkey_cnt_) ? pos : pos + 2;
      if (OB_FAIL(m.delta_pk_pos_.push_back(dpos))) {}
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < stmt.val_cols().count(); ++i) {
    int64_t pos = -1;
    if (OB_FAIL(find_pos(stmt.val_cols().at(i), pos))) {
    } else if (OB_FAIL(m.val_pos_.push_back(pos))) {
    } else {
      const int64_t dpos = (pos < m.rowkey_cnt_) ? pos : pos + 2;
      if (OB_FAIL(m.delta_val_pos_.push_back(dpos))) {}
    }
  }
  return ret;
}

// Fill an ObObj cell from a storage datum + output column metadata.
// Collection columns (vector/array) are emitted with their true SQL
// type and subschema id; the protocol UDT helper (process_sql_udt_results)
// will render the binary to client-visible form. The subschema was
// registered into plan_ctx during CG and propagated into phy_plan, so
// the response path can resolve it.
int set_obj_(const blocksstable::ObStorageDatum &d, const ObDiffOutputCol &oc, ObObj &out)
{
  int ret = OB_SUCCESS;
  if (d.is_null() || d.is_nop()) {
    out.set_null();
  } else if (ob_is_collection_sql_type(oc.obj_type_)) {
    if (OB_UNLIKELY(oc.subschema_id_ == UINT16_MAX)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("collection col missing subschema id", K(ret), K(oc));
    } else {
      ObObjMeta meta;
      meta.set_collection(oc.subschema_id_);
      meta.set_has_lob_header();
      if (OB_FAIL(d.to_obj_enhance(out, meta))) {
        LOG_WARN("collection to_obj failed", K(ret));
      } else if (OB_LIKELY(!out.is_null())) {
        // Storage already stores collection bytes with a lob header
        // (collections are lob-backed). Ensure the cell meta agrees so
        // ob_adjust_lob_datum + the protocol UDT helper don't reject.
        out.set_has_lob_header();
      }
    }
  } else {
    ObObjMeta meta;
    meta.set_type(oc.obj_type_);
    meta.set_collation_type(oc.collation_type_);
    ret = d.to_obj_enhance(out, meta);
  }
  return ret;
}

// Allocate and populate the __table cell.
int set_table_cell_(ObIAllocator &alloc, const ObString &db, const ObString &tbl,
                    ObObj &out)
{
  int ret = OB_SUCCESS;
  const int64_t need = db.length() + 1 + tbl.length();
  char *buf = static_cast<char *>(alloc.alloc(need));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(buf, db.ptr(), db.length());
    buf[db.length()] = '.';
    MEMCPY(buf + db.length() + 1, tbl.ptr(), tbl.length());
    out.set_varchar(ObString(need, buf));
    out.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
  }
  return ret;
}

int set_flag_cell_(ObIAllocator &alloc, ObObj &out)
{
  int ret = OB_SUCCESS;
  static const char *kFlag = "INSERT";
  char *buf = static_cast<char *>(alloc.alloc(6));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(buf, kFlag, 6);
    out.set_varchar(ObString(6, buf));
    out.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
  }
  return ret;
}

// Emit an output row built from a *point-getter* row (storage-ordered, no
// multi-version pseudo cols). All schema positions are direct indices.
int emit_from_point_(ObIAllocator &alloc,
                     ObDiffTableStmt &stmt,
                     const DiffColMap &map,
                     DiffSide side,
                     const blocksstable::ObDatumRow &row,
                     ObIArray<ObNewRow *> &out)
{
  int ret = OB_SUCCESS;
  const int64_t out_cnt = stmt.out_cols().count();
  void *row_buf = alloc.alloc(sizeof(ObNewRow));
  void *cells_buf = alloc.alloc(sizeof(ObObj) * out_cnt);
  if (OB_ISNULL(row_buf) || OB_ISNULL(cells_buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    ObNewRow *nr = new (row_buf) ObNewRow();
    nr->cells_ = new (cells_buf) ObObj[out_cnt];
    nr->count_ = out_cnt;
    const ObString &db = (side == DiffSide::CURRENT_SIDE) ? stmt.get_cur_db() : stmt.get_inc_db();
    const ObString &tb = (side == DiffSide::CURRENT_SIDE) ? stmt.get_cur_table() : stmt.get_inc_table();
    if (OB_FAIL(set_table_cell_(alloc, db, tb, nr->cells_[0]))) {
    } else if (OB_FAIL(set_flag_cell_(alloc, nr->cells_[1]))) {
    } else {
      int64_t out_idx = 2;
      // PK
      for (int64_t i = 0; OB_SUCC(ret) && i < map.pk_pos_.count(); ++i) {
        const int64_t p = map.pk_pos_.at(i);
        if (p < 0 || p >= row.count_) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("pk col pos out of range", K(ret), K(p), K(row.count_));
        } else if (OB_FAIL(set_obj_(row.storage_datums_[p],
                                    stmt.out_cols().at(out_idx),
                                    nr->cells_[out_idx]))) {
          LOG_WARN("set pk obj failed", K(ret), K(i));
        } else {
          ++out_idx;
        }
      }
      // VAL
      for (int64_t i = 0; OB_SUCC(ret) && i < map.val_pos_.count(); ++i) {
        const int64_t p = map.val_pos_.at(i);
        if (p < 0 || p >= row.count_) {
          nr->cells_[out_idx].set_null();
        } else if (OB_FAIL(set_obj_(row.storage_datums_[p],
                                    stmt.out_cols().at(out_idx),
                                    nr->cells_[out_idx]))) {
          LOG_WARN("set val obj failed", K(ret), K(i));
        }
        ++out_idx;
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(out.push_back(nr))) {
      LOG_WARN("push row failed", K(ret));
    }
  }
  return ret;
}

// Compare value columns between two point-getter rows. NULL-safe.
bool point_rows_val_equal_(const DiffColMap &map,
                           const blocksstable::ObDatumRow &a,
                           const blocksstable::ObDatumRow &b)
{
  for (int64_t i = 0; i < map.val_pos_.count(); ++i) {
    const int64_t p = map.val_pos_.at(i);
    if (p < 0) continue;
    const bool a_missing = (p >= a.count_);
    const bool b_missing = (p >= b.count_);
    if (a_missing && b_missing) continue;
    if (a_missing || b_missing) return false;
    if (!nullsafe_eq_(a.storage_datums_[p], b.storage_datums_[p])) return false;
  }
  return true;
}

// Emit a single output row from a delta-row (multi-version layout).
int emit_from_delta_(ObIAllocator &alloc,
                     ObDiffTableStmt &stmt,
                     const DiffColMap &map,
                     DiffSide side,
                     const ObDiffMaterializedRow &mv,
                     ObIArray<ObNewRow *> &out)
{
  int ret = OB_SUCCESS;
  const int64_t out_cnt = stmt.out_cols().count();
  void *row_buf = alloc.alloc(sizeof(ObNewRow));
  void *cells_buf = alloc.alloc(sizeof(ObObj) * out_cnt);
  if (OB_ISNULL(row_buf) || OB_ISNULL(cells_buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    ObNewRow *nr = new (row_buf) ObNewRow();
    nr->cells_ = new (cells_buf) ObObj[out_cnt];
    nr->count_ = out_cnt;
    const ObString &db = (side == DiffSide::CURRENT_SIDE) ? stmt.get_cur_db() : stmt.get_inc_db();
    const ObString &tb = (side == DiffSide::CURRENT_SIDE) ? stmt.get_cur_table() : stmt.get_inc_table();
    if (OB_FAIL(set_table_cell_(alloc, db, tb, nr->cells_[0]))) {
    } else if (OB_FAIL(set_flag_cell_(alloc, nr->cells_[1]))) {
    } else {
      int64_t out_idx = 2;
      for (int64_t i = 0; OB_SUCC(ret) && i < map.delta_pk_pos_.count(); ++i) {
        const int64_t p = map.delta_pk_pos_.at(i);
        if (p < 0 || p >= mv.row_.count_) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(set_obj_(mv.row_.storage_datums_[p],
                                    stmt.out_cols().at(out_idx),
                                    nr->cells_[out_idx]))) {
        }
        ++out_idx;
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < map.delta_val_pos_.count(); ++i) {
        const int64_t p = map.delta_val_pos_.at(i);
        if (p < 0 || p >= mv.row_.count_) {
          nr->cells_[out_idx].set_null();
        } else if (OB_FAIL(set_obj_(mv.row_.storage_datums_[p],
                                    stmt.out_cols().at(out_idx),
                                    nr->cells_[out_idx]))) {
        }
        ++out_idx;
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(out.push_back(nr))) {}
  }
  return ret;
}

// Compare USER PK columns of two delta-scanner rows.
int user_pk_cmp_delta_(const DiffColMap &map,
                       const ObDiffMaterializedRow &a,
                       const ObDiffMaterializedRow &b,
                       int &cmp)
{
  int ret = OB_SUCCESS;
  cmp = 0;
  for (int64_t i = 0; OB_SUCC(ret) && 0 == cmp && i < map.delta_pk_pos_.count(); ++i) {
    const int64_t p = map.delta_pk_pos_.at(i);
    if (p < 0 || p >= a.row_.count_ || p >= b.row_.count_) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const blocksstable::ObStorageDatum &da = a.row_.storage_datums_[p];
      const blocksstable::ObStorageDatum &db_ = b.row_.storage_datums_[p];
      if (da.is_null() && db_.is_null()) { /* eq */ }
      else if (da.is_null()) { cmp = -1; }
      else if (db_.is_null()) { cmp = 1; }
      else {
        // Length-then-byte order. For INT this is deterministic (8 bytes
        // little-endian) and for VARCHAR/BINARY it sorts by length then
        // bytes. Cross-side rows will be ordered consistently.
        if (da.len_ != db_.len_) cmp = (da.len_ < db_.len_) ? -1 : 1;
        else cmp = MEMCMP(da.ptr_, db_.ptr_, da.len_);
      }
    }
  }
  return ret;
}

// Compare USER VAL columns of two delta-scanner rows (null-safe). True if equal.
bool delta_rows_val_equal_(const DiffColMap &map,
                           const ObDiffMaterializedRow &a,
                           const ObDiffMaterializedRow &b)
{
  for (int64_t i = 0; i < map.delta_val_pos_.count(); ++i) {
    const int64_t p = map.delta_val_pos_.at(i);
    if (p < 0) continue;
    const bool a_missing = (p >= a.row_.count_);
    const bool b_missing = (p >= b.row_.count_);
    if (a_missing && b_missing) continue;
    if (a_missing || b_missing) return false;
    if (!nullsafe_eq_(a.row_.storage_datums_[p], b.row_.storage_datums_[p])) return false;
  }
  return true;
}

// Walk a tablet's fork ancestry chain. For each step record (tablet_id,
// snap_to_parent). The chain ends when a tablet has no fork lineage (root)
// or we exceed kMaxDepth (cycle defense).
struct LineageStep
{
  ObTabletID tablet_id_;
  int64_t snap_to_parent_;
  TO_STRING_KV(K_(tablet_id), K_(snap_to_parent));
};

int walk_lineage_chain_(ObLS &ls, ObTabletID start,
                        ObIArray<LineageStep> &chain)
{
  int ret = OB_SUCCESS;
  static const int64_t kMaxDepth = 64;
  ObTabletID cur = start;
  for (int64_t i = 0; OB_SUCC(ret) && i < kMaxDepth; ++i) {
    ObTabletHandle th;
    if (OB_FAIL(ls.get_tablet(cur, th))) {
      LOG_WARN("walk lineage: get tablet failed", K(ret), K(cur));
      break;
    }
    ObForkTabletInfo fi;
    int gr = th.get_obj()->get_fork_info(fi);
    LineageStep step;
    step.tablet_id_ = cur;
    if (OB_SUCCESS == gr && fi.is_valid()) {
      step.snap_to_parent_ = fi.get_fork_snapshot_version();
    } else {
      step.snap_to_parent_ = 0;
    }
    if (OB_FAIL(chain.push_back(step))) break;
    if (OB_SUCCESS != gr || !fi.is_valid()) break;  // root reached
    cur = fi.get_fork_src_tablet_id();
  }
  return ret;
}

// LCA-based fork-snap computation. Walks both chains, finds the lowest
// common ancestor by tablet_id intersection, and uses the minimum snap on
// either path to LCA as the conservative cut-off SCN. Returns 0 if no
// lineage relationship exists.
int detect_lineage_(uint64_t tenant_id,
                    const ObTableSchema &cur_schema,
                    const ObTableSchema &inc_schema,
                    int64_t &fork_snap)
{
  int ret = OB_SUCCESS;
  fork_snap = 0;
  ObSEArray<ObTabletID, 4> cur_tabs, inc_tabs;
  if (OB_FAIL(cur_schema.get_tablet_ids(cur_tabs))) {
  } else if (OB_FAIL(inc_schema.get_tablet_ids(inc_tabs))) {
  } else if (cur_tabs.empty() || inc_tabs.empty()) {
    // nothing to do
  } else {
    ObSEArray<LineageStep, 8> cur_chain, inc_chain;
    MTL_SWITCH(tenant_id) {
      ObLSService *svc = MTL(ObLSService *);
      ObLSHandle lsh;
      if (OB_ISNULL(svc)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(svc->get_ls(SYS_LS, lsh, ObLSGetMod::TABLET_MOD))) {
      } else {
        if (OB_FAIL(walk_lineage_chain_(*lsh.get_ls(), cur_tabs.at(0), cur_chain))) {
          LOG_WARN("walk cur chain failed", K(ret));
        } else if (OB_FAIL(walk_lineage_chain_(*lsh.get_ls(), inc_tabs.at(0), inc_chain))) {
          LOG_WARN("walk inc chain failed", K(ret));
        }
      }
    }
    if (OB_FAIL(ret)) { ret = OB_SUCCESS; return ret; }  // soft fail

    // Find LCA: smallest i such that cur_chain[i].tablet_id ∈ inc_chain.
    int64_t cur_idx = -1, inc_idx = -1;
    for (int64_t i = 0; cur_idx < 0 && i < cur_chain.count(); ++i) {
      for (int64_t j = 0; j < inc_chain.count(); ++j) {
        if (cur_chain.at(i).tablet_id_ == inc_chain.at(j).tablet_id_) {
          cur_idx = i;
          inc_idx = j;
          break;
        }
      }
    }
    if (cur_idx < 0) {
      // No shared ancestor in tablet chain → fall back.
      return ret;
    }
    // Conservative cut-off = min of all fork snaps on the path-to-LCA on
    // either side. snap_to_parent_[k] is the SCN at which step k branched
    // from step k+1 (its parent on the chain).
    int64_t min_snap = INT64_MAX;
    for (int64_t i = 0; i < cur_idx; ++i) {
      const int64_t s = cur_chain.at(i).snap_to_parent_;
      if (s > 0 && s < min_snap) min_snap = s;
    }
    for (int64_t j = 0; j < inc_idx; ++j) {
      const int64_t s = inc_chain.at(j).snap_to_parent_;
      if (s > 0 && s < min_snap) min_snap = s;
    }
    if (cur_idx == 0 && inc_idx == 0) {
      // Same tablet — same table (DIFF on self). Boundary = current latest
      // SCN; both delta scanners will be empty → output empty.
      // We choose any positive value; INT64_MAX/2 keeps the path
      // consistent with INCREMENTAL semantics.
      fork_snap = INT64_MAX / 2;
    } else if (cur_idx > 0 || inc_idx > 0) {
      fork_snap = (min_snap == INT64_MAX) ? 0 : min_snap;
    }
  }
  return ret;
}

// ===== Per-tablet diff drivers ============================================

// INCREMENTAL: use delta scanners to enumerate candidate PKs (post-fork
// modified PKs, including tombstones), then point-get on both sides (full
// history) and classify against the *current* state at the read snapshot.
int run_pair_incremental_(uint64_t tenant_id,
                          uint64_t cur_tid, uint64_t inc_tid,
                          ObTabletID cur_tabid, ObTabletID inc_tabid,
                          int64_t fork_snap,
                          ObDiffTableStmt &stmt,
                          const DiffColMap &map,
                          ObIAllocator &alloc,
                          ObIArray<ObNewRow *> &out)
{
  int ret = OB_SUCCESS;
  ObDiffTabletScanner cur_delta, inc_delta;
  ObTabletPointGetter cur_pg, inc_pg;
  if (OB_FAIL(cur_delta.init(tenant_id, SYS_LS, cur_tabid, cur_tid, fork_snap, 0, alloc))) {
    LOG_WARN("cur delta init failed", K(ret));
  } else if (OB_FAIL(inc_delta.init(tenant_id, SYS_LS, inc_tabid, inc_tid, fork_snap, 0, alloc))) {
    LOG_WARN("inc delta init failed", K(ret));
  } else if (OB_FAIL(cur_pg.init(tenant_id, SYS_LS, cur_tabid, cur_tid, SCN::max_scn()))) {
    LOG_WARN("cur pg init failed", K(ret));
  } else if (OB_FAIL(inc_pg.init(tenant_id, SYS_LS, inc_tabid, inc_tid, SCN::max_scn()))) {
    LOG_WARN("inc pg init failed", K(ret));
  } else {
    const blocksstable::ObStorageDatumUtils *du = cur_delta.get_datum_utils();
    if (OB_ISNULL(du)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("datum utils null", K(ret));
    } else {
      const int64_t rk_n = cur_delta.get_rowkey_cnt();
      const ObDiffMaterializedRow *c_dr = nullptr, *i_dr = nullptr;
      int cr = cur_delta.get_next_row(c_dr);
      int ir = inc_delta.get_next_row(i_dr);
      if (OB_ITER_END == cr) { c_dr = nullptr; cr = OB_SUCCESS; }
      if (OB_ITER_END == ir) { i_dr = nullptr; ir = OB_SUCCESS; }
      if (OB_SUCCESS != cr) ret = cr;
      else if (OB_SUCCESS != ir) ret = ir;

      while (OB_SUCC(ret) && (c_dr != nullptr || i_dr != nullptr)) {
        // Choose next candidate PK by merge order.
        blocksstable::ObDatumRowkey cand_pk;
        int cmp = 0;
        bool advance_c = false, advance_i = false;
        if (c_dr != nullptr && i_dr != nullptr) {
          blocksstable::ObDatumRowkey ka, kb;
          if (OB_FAIL(make_rowkey_(c_dr->row_, rk_n, map.rk_col_descs_, alloc, ka))) break;
          if (OB_FAIL(make_rowkey_(i_dr->row_, rk_n, map.rk_col_descs_, alloc, kb))) break;
          if (OB_FAIL(rowkey_cmp_(ka, kb, *du, cmp))) break;
          if (cmp <= 0) cand_pk = ka, advance_c = true;
          if (cmp >= 0) {
            if (!advance_c) cand_pk = kb;
            advance_i = true;
          }
        } else if (c_dr != nullptr) {
          if (OB_FAIL(make_rowkey_(c_dr->row_, rk_n, map.rk_col_descs_, alloc, cand_pk))) break;
          advance_c = true;
        } else {
          if (OB_FAIL(make_rowkey_(i_dr->row_, rk_n, map.rk_col_descs_, alloc, cand_pk))) break;
          advance_i = true;
        }

        // Two point gets, full history.
        const blocksstable::ObDatumRow *r_cur = nullptr, *r_inc = nullptr;
        int gr_c = cur_pg.get(cand_pk, r_cur);
        int gr_i = inc_pg.get(cand_pk, r_inc);
        if (OB_SUCCESS != gr_c && OB_ENTRY_NOT_EXIST != gr_c) {
          ret = gr_c; LOG_WARN("cur pg failed", K(ret), K(cand_pk)); break;
        }
        if (OB_SUCCESS != gr_i && OB_ENTRY_NOT_EXIST != gr_i) {
          ret = gr_i; LOG_WARN("inc pg failed", K(ret), K(cand_pk)); break;
        }
        const bool has_cur = (OB_SUCCESS == gr_c);
        const bool has_inc = (OB_SUCCESS == gr_i);

        if (!has_cur && !has_inc) {
          // both sides absent at read snapshot → nothing to emit
        } else if (has_cur && !has_inc) {
          if (OB_FAIL(emit_from_point_(alloc, stmt, map,
                                       DiffSide::CURRENT_SIDE, *r_cur, out))) break;
        } else if (!has_cur && has_inc) {
          if (OB_FAIL(emit_from_point_(alloc, stmt, map,
                                       DiffSide::INCOMING_SIDE, *r_inc, out))) break;
        } else {
          if (!point_rows_val_equal_(map, *r_cur, *r_inc)) {
            // Emit in lexical order of "db.table" to keep output stable
            // and to match the recorded mysqltest result baseline.
            const bool inc_first = stmt.get_inc_db().compare(stmt.get_cur_db()) < 0
                || (stmt.get_inc_db().compare(stmt.get_cur_db()) == 0
                    && stmt.get_inc_table().compare(stmt.get_cur_table()) < 0);
            if (inc_first) {
              if (OB_FAIL(emit_from_point_(alloc, stmt, map,
                                           DiffSide::INCOMING_SIDE, *r_inc, out))) break;
              if (OB_FAIL(emit_from_point_(alloc, stmt, map,
                                           DiffSide::CURRENT_SIDE, *r_cur, out))) break;
            } else {
              if (OB_FAIL(emit_from_point_(alloc, stmt, map,
                                           DiffSide::CURRENT_SIDE, *r_cur, out))) break;
              if (OB_FAIL(emit_from_point_(alloc, stmt, map,
                                           DiffSide::INCOMING_SIDE, *r_inc, out))) break;
            }
          }
        }

        // advance source iterators that contributed to this candidate
        if (advance_c) {
          cr = cur_delta.get_next_row(c_dr);
          if (OB_ITER_END == cr) { c_dr = nullptr; cr = OB_SUCCESS; }
          if (OB_SUCCESS != cr) { ret = cr; break; }
        }
        if (advance_i) {
          ir = inc_delta.get_next_row(i_dr);
          if (OB_ITER_END == ir) { i_dr = nullptr; ir = OB_SUCCESS; }
          if (OB_SUCCESS != ir) { ret = ir; break; }
        }
      }
    }
  }
  return ret;
}

// FALLBACK: no lineage. Same shape as the incremental path but the delta
// scanners cover the entire history (fork_snap=0). Candidate PK = the
// union of cur/inc storage rowkeys; for each, double point-get classifies.
// Note this is *not* user-PK correct for heap tables (storage rowkey is
// hidden_pk, distinct on each side). Heap fallback is handled by a
// dedicated path below.
int run_pair_fallback_point_(uint64_t tenant_id,
                             uint64_t cur_tid, uint64_t inc_tid,
                             ObTabletID cur_tabid, ObTabletID inc_tabid,
                             ObDiffTableStmt &stmt,
                             const DiffColMap &map,
                             ObIAllocator &alloc,
                             ObIArray<ObNewRow *> &out)
{
  int ret = OB_SUCCESS;
  ObDiffTabletScanner cur_scan, inc_scan;
  ObTabletPointGetter cur_pg, inc_pg;
  if (OB_FAIL(cur_scan.init(tenant_id, SYS_LS, cur_tabid, cur_tid, 0, 0, alloc))) {
    LOG_WARN("cur full init failed", K(ret));
  } else if (OB_FAIL(inc_scan.init(tenant_id, SYS_LS, inc_tabid, inc_tid, 0, 0, alloc))) {
    LOG_WARN("inc full init failed", K(ret));
  } else if (OB_FAIL(cur_pg.init(tenant_id, SYS_LS, cur_tabid, cur_tid, SCN::max_scn()))) {
    LOG_WARN("cur pg init failed", K(ret));
  } else if (OB_FAIL(inc_pg.init(tenant_id, SYS_LS, inc_tabid, inc_tid, SCN::max_scn()))) {
    LOG_WARN("inc pg init failed", K(ret));
  } else {
    const blocksstable::ObStorageDatumUtils *du = cur_scan.get_datum_utils();
    if (OB_ISNULL(du)) { ret = OB_ERR_UNEXPECTED; }
    else {
      const int64_t rk_n = cur_scan.get_rowkey_cnt();
      const ObDiffMaterializedRow *c_dr = nullptr, *i_dr = nullptr;
      int cr = cur_scan.get_next_row(c_dr);
      int ir = inc_scan.get_next_row(i_dr);
      if (OB_ITER_END == cr) { c_dr = nullptr; cr = OB_SUCCESS; }
      if (OB_ITER_END == ir) { i_dr = nullptr; ir = OB_SUCCESS; }

      while (OB_SUCC(ret) && (c_dr != nullptr || i_dr != nullptr)) {
        blocksstable::ObDatumRowkey cand_pk;
        int cmp = 0;
        bool adv_c = false, adv_i = false;
        if (c_dr && i_dr) {
          blocksstable::ObDatumRowkey ka, kb;
          if (OB_FAIL(make_rowkey_(c_dr->row_, rk_n, map.rk_col_descs_, alloc, ka))) break;
          if (OB_FAIL(make_rowkey_(i_dr->row_, rk_n, map.rk_col_descs_, alloc, kb))) break;
          if (OB_FAIL(rowkey_cmp_(ka, kb, *du, cmp))) break;
          if (cmp <= 0) { cand_pk = ka; adv_c = true; }
          if (cmp >= 0) { if (!adv_c) cand_pk = kb; adv_i = true; }
        } else if (c_dr) {
          if (OB_FAIL(make_rowkey_(c_dr->row_, rk_n, map.rk_col_descs_, alloc, cand_pk))) break;
          adv_c = true;
        } else {
          if (OB_FAIL(make_rowkey_(i_dr->row_, rk_n, map.rk_col_descs_, alloc, cand_pk))) break;
          adv_i = true;
        }

        const blocksstable::ObDatumRow *r_cur = nullptr, *r_inc = nullptr;
        int gr_c = cur_pg.get(cand_pk, r_cur);
        int gr_i = inc_pg.get(cand_pk, r_inc);
        if (OB_SUCCESS != gr_c && OB_ENTRY_NOT_EXIST != gr_c) { ret = gr_c; break; }
        if (OB_SUCCESS != gr_i && OB_ENTRY_NOT_EXIST != gr_i) { ret = gr_i; break; }
        const bool has_cur = (OB_SUCCESS == gr_c);
        const bool has_inc = (OB_SUCCESS == gr_i);

        if (!has_cur && !has_inc) {
        } else if (has_cur && !has_inc) {
          if (OB_FAIL(emit_from_point_(alloc, stmt, map, DiffSide::CURRENT_SIDE, *r_cur, out))) break;
        } else if (!has_cur && has_inc) {
          if (OB_FAIL(emit_from_point_(alloc, stmt, map, DiffSide::INCOMING_SIDE, *r_inc, out))) break;
        } else if (!point_rows_val_equal_(map, *r_cur, *r_inc)) {
          const bool inc_first = stmt.get_inc_db().compare(stmt.get_cur_db()) < 0
              || (stmt.get_inc_db().compare(stmt.get_cur_db()) == 0
                  && stmt.get_inc_table().compare(stmt.get_cur_table()) < 0);
          if (inc_first) {
            if (OB_FAIL(emit_from_point_(alloc, stmt, map, DiffSide::INCOMING_SIDE, *r_inc, out))) break;
            if (OB_FAIL(emit_from_point_(alloc, stmt, map, DiffSide::CURRENT_SIDE, *r_cur, out))) break;
          } else {
            if (OB_FAIL(emit_from_point_(alloc, stmt, map, DiffSide::CURRENT_SIDE, *r_cur, out))) break;
            if (OB_FAIL(emit_from_point_(alloc, stmt, map, DiffSide::INCOMING_SIDE, *r_inc, out))) break;
          }
        }

        if (adv_c) {
          cr = cur_scan.get_next_row(c_dr);
          if (OB_ITER_END == cr) { c_dr = nullptr; cr = OB_SUCCESS; }
          if (OB_SUCCESS != cr) { ret = cr; break; }
        }
        if (adv_i) {
          ir = inc_scan.get_next_row(i_dr);
          if (OB_ITER_END == ir) { i_dr = nullptr; ir = OB_SUCCESS; }
          if (OB_SUCCESS != ir) { ret = ir; break; }
        }
      }
    }
  }
  return ret;
}

// FALLBACK: no lineage. Sort-merge full scans on both sides by USER PK
// (not storage rowkey — heap tables have hidden_pk as storage rowkey but
// must diff by the user-declared primary key). Emit directly from the
// scanner rows so column mapping handles both normal and heap layouts.
int run_pair_fallback_(uint64_t tenant_id,
                       uint64_t cur_tid, uint64_t inc_tid,
                       ObTabletID cur_tabid, ObTabletID inc_tabid,
                       ObDiffTableStmt &stmt,
                       const DiffColMap &map,
                       ObIAllocator &alloc,
                       ObIArray<ObNewRow *> &out)
{
  int ret = OB_SUCCESS;
  ObDiffTabletScanner cur_scan, inc_scan;
  if (OB_FAIL(cur_scan.init(tenant_id, SYS_LS, cur_tabid, cur_tid, 0, 0, alloc))) {
    LOG_WARN("cur full init failed", K(ret));
  } else if (OB_FAIL(inc_scan.init(tenant_id, SYS_LS, inc_tabid, inc_tid, 0, 0, alloc))) {
    LOG_WARN("inc full init failed", K(ret));
  } else {
    // Materialize both sides, then sort by user PK in-memory. Scanner
    // already orders by *storage rowkey*; for normal tables storage rowkey
    // == user PK so we get the right order for free, but for heap the
    // storage rowkey is hidden_pk and we must re-sort.
    ObSEArray<const ObDiffMaterializedRow *, 64> cur_rows, inc_rows;
    {
      const ObDiffMaterializedRow *r = nullptr;
      while (OB_SUCC(ret)) {
        int rr = cur_scan.get_next_row(r);
        if (OB_ITER_END == rr) break;
        if (OB_SUCCESS != rr) { ret = rr; break; }
        if (r->is_delete_) continue;  // skip tombstones
        if (OB_FAIL(cur_rows.push_back(r))) break;
      }
    }
    if (OB_SUCC(ret)) {
      const ObDiffMaterializedRow *r = nullptr;
      while (OB_SUCC(ret)) {
        int rr = inc_scan.get_next_row(r);
        if (OB_ITER_END == rr) break;
        if (OB_SUCCESS != rr) { ret = rr; break; }
        if (r->is_delete_) continue;
        if (OB_FAIL(inc_rows.push_back(r))) break;
      }
    }
    // Stable in-place sort by user PK using insertion sort. Diff is a
    // DDL/analysis path; we don't expect huge fanout per partition.
    auto sort_by_user_pk = [&](ObSEArray<const ObDiffMaterializedRow *, 64> &v) -> int {
      int sret = OB_SUCCESS;
      for (int64_t i = 1; OB_SUCC(sret) && i < v.count(); ++i) {
        const ObDiffMaterializedRow *cur = v.at(i);
        int64_t j = i - 1;
        while (j >= 0) {
          int cmp = 0;
          if (OB_FAIL(user_pk_cmp_delta_(map, *v.at(j), *cur, cmp))) { sret = ret; break; }
          if (cmp <= 0) break;
          v.at(j + 1) = v.at(j);
          --j;
        }
        if (OB_SUCC(sret)) v.at(j + 1) = cur;
      }
      return sret;
    };
    if (OB_SUCC(ret) && OB_FAIL(sort_by_user_pk(cur_rows))) {}
    if (OB_SUCC(ret) && OB_FAIL(sort_by_user_pk(inc_rows))) {}

    // Outer-merge by user PK.
    int64_t ci = 0, ii = 0;
    while (OB_SUCC(ret) && (ci < cur_rows.count() || ii < inc_rows.count())) {
      int cmp = 0;
      bool adv_c = false, adv_i = false;
      const ObDiffMaterializedRow *r_cur = (ci < cur_rows.count()) ? cur_rows.at(ci) : nullptr;
      const ObDiffMaterializedRow *r_inc = (ii < inc_rows.count()) ? inc_rows.at(ii) : nullptr;
      if (r_cur && r_inc) {
        if (OB_FAIL(user_pk_cmp_delta_(map, *r_cur, *r_inc, cmp))) break;
      } else {
        cmp = r_cur == nullptr ? 1 : -1;
      }
      if (cmp < 0) {
        if (OB_FAIL(emit_from_delta_(alloc, stmt, map, DiffSide::CURRENT_SIDE, *r_cur, out))) break;
        adv_c = true;
      } else if (cmp > 0) {
        if (OB_FAIL(emit_from_delta_(alloc, stmt, map, DiffSide::INCOMING_SIDE, *r_inc, out))) break;
        adv_i = true;
      } else {
        if (!delta_rows_val_equal_(map, *r_cur, *r_inc)) {
          const bool inc_first = stmt.get_inc_db().compare(stmt.get_cur_db()) < 0
              || (stmt.get_inc_db().compare(stmt.get_cur_db()) == 0
                  && stmt.get_inc_table().compare(stmt.get_cur_table()) < 0);
          if (inc_first) {
            if (OB_FAIL(emit_from_delta_(alloc, stmt, map, DiffSide::INCOMING_SIDE, *r_inc, out))) break;
            if (OB_FAIL(emit_from_delta_(alloc, stmt, map, DiffSide::CURRENT_SIDE, *r_cur, out))) break;
          } else {
            if (OB_FAIL(emit_from_delta_(alloc, stmt, map, DiffSide::CURRENT_SIDE, *r_cur, out))) break;
            if (OB_FAIL(emit_from_delta_(alloc, stmt, map, DiffSide::INCOMING_SIDE, *r_inc, out))) break;
          }
        }
        adv_c = adv_i = true;
      }
      if (adv_c) ++ci;
      if (adv_i) ++ii;
    }
  }
  return ret;
}

}  // anonymous namespace

int ObDiffTableOpCompute::compute_diff_rows(
    const ObDiffTableStmt &param,
    share::schema::ObSchemaGetterGuard &schema_guard,
    ObSQLSessionInfo &session,
    common::ObIAllocator &alloc,
    common::ObIArray<common::ObNewRow *> &out_rows)
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaGetterGuard *guard = &schema_guard;
  UNUSED(session);
  ObDiffTableStmt &stmt = const_cast<ObDiffTableStmt &>(param);

  const ObTableSchema *cur_schema = nullptr, *inc_schema = nullptr;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(guard->get_table_schema(stmt.get_tenant_id(), stmt.get_cur_table_id(), cur_schema))) {
    } else if (OB_FAIL(guard->get_table_schema(stmt.get_tenant_id(), stmt.get_inc_table_id(), inc_schema))) {
    } else if (OB_ISNULL(cur_schema) || OB_ISNULL(inc_schema)) {
      ret = OB_TABLE_NOT_EXIST;
    }
  }

  DiffColMap col_map;
  if (OB_SUCC(ret) && OB_FAIL(build_col_map_(*cur_schema, stmt, col_map))) {
    LOG_WARN("build col map failed", K(ret));
  }

  int64_t fork_snap = 0;
  if (OB_SUCC(ret)) {
    detect_lineage_(stmt.get_tenant_id(), *cur_schema, *inc_schema, fork_snap);
    LOG_INFO("DIFF TABLE lineage",
             "mode", fork_snap > 0 ? "INCREMENTAL" : "FALLBACK",
             K(fork_snap));
  }

  ObSEArray<ObTabletID, 4> cur_tabs, inc_tabs;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(cur_schema->get_tablet_ids(cur_tabs))) {
    } else if (OB_FAIL(inc_schema->get_tablet_ids(inc_tabs))) {
    } else if (cur_tabs.count() != inc_tabs.count()) {
      LOG_WARN("DIFF tablet count mismatch — forcing fallback",
               "cur", cur_tabs.count(), "inc", inc_tabs.count());
      fork_snap = 0;
    }
  }

  const bool is_heap = (cur_schema != nullptr && cur_schema->is_heap_organized_table());

  const int64_t pair_count = MIN(cur_tabs.count(), inc_tabs.count());
  for (int64_t p = 0; OB_SUCC(ret) && p < pair_count; ++p) {
    if (fork_snap > 0) {
      ret = run_pair_incremental_(stmt.get_tenant_id(),
                                  stmt.get_cur_table_id(), stmt.get_inc_table_id(),
                                  cur_tabs.at(p), inc_tabs.at(p),
                                  fork_snap, stmt, col_map, alloc, out_rows);
    } else if (is_heap) {
      ret = run_pair_fallback_(stmt.get_tenant_id(),
                               stmt.get_cur_table_id(), stmt.get_inc_table_id(),
                               cur_tabs.at(p), inc_tabs.at(p),
                               stmt, col_map, alloc, out_rows);
    } else {
      ret = run_pair_fallback_point_(stmt.get_tenant_id(),
                                     stmt.get_cur_table_id(), stmt.get_inc_table_id(),
                                     cur_tabs.at(p), inc_tabs.at(p),
                                     stmt, col_map, alloc, out_rows);
    }
    if (OB_FAIL(ret)) {
      LOG_WARN("diff pair failed", K(ret), K(p));
    }
  }

  // Drain the collected rows into the stmt's ObRowStore (the row_store_
  // is later read by ObValuesOp via ObDiffTableLogPlan).
  for (int64_t i = 0; OB_SUCC(ret) && i < out_rows.count(); ++i) {
    if (OB_ISNULL(out_rows.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("DIFF TABLE done",
             "mode", fork_snap > 0 ? "INCREMENTAL" : "FALLBACK",
             K(fork_snap), "rows", out_rows.count());
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
