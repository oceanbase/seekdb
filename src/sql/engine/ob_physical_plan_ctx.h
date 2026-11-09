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

#ifndef OCEANBASE_SQL_OB_PHYSICAL_PLAN_CTX_H
#define OCEANBASE_SQL_OB_PHYSICAL_PLAN_CTX_H
#include "common/datum/datum.h"
#include "common/datum/datum_parameter_metadata.h"
#include "common/datum/native_typed_datum.h"
#include "common/datum/datum_parameter_array.h"
#include "share/ob_autoincrement_param.h"
#include "share/ob_tablet_autoincrement_param.h"
#include "common/sql_mode/ob_sql_mode.h"
#include "query/engine/statement_time.h"
#include "common/ob_field.h"
#include "lib/time/ob_clock_generator.h"
#include "data_plane/transaction/ob_tx_options.h"
#include "lib/container/ob_fixed_array.h"
#include "lib/container/ob_2d_array.h"
#include "sql/plan_cache/ob_plan_cache_util.h"
#include "sql/engine/expr/ob_expr.h"
#include "common/udt/ob_udt_type.h"
#include "common/enumset/ob_enum_set_meta.h"
#include "sql/engine/ob_subschema_ctx.h"
#include "sql/engine/expr/ob_expr_util.h"

namespace oceanbase
{
namespace pl { class ObPLExecState; }
namespace sql
{
class ObResultSet;
class ObSQLSessionInfo;
class ObPhysicalPlan;
class ObExecContext;
struct ObPxDmlRowInfo;
class ObPhysicalPlanCtx;

// A same-execution binding backup for the existing shallow batch-result holder.
// Payload spans retain their statement owner; this token cannot extend it.
class NativeParameterSlotSnapshot
{
public:
  NativeParameterSlotSnapshot() : owner_(nullptr), generation_(0), index_(-1), value_(), attributes_(), word_(0), state_(0) {}
  int64_t index() const { return index_; }
  const common::NativeTypedDatum &value() const { return value_; }
  const common::DatumParameterMetadata &attributes() const { return attributes_; }
private:
  friend class ObPhysicalPlanCtx;
  const ObPhysicalPlanCtx *owner_;
  uint64_t generation_;
  int64_t index_;
  common::NativeTypedDatum value_;
  common::DatumParameterMetadata attributes_;
  uint64_t word_;
  uint8_t state_;
};

// Sparse actual SQL-array input carried beside its selected scalar slot.
// Types and spans borrow the receiving execution owner, never wire addresses.
struct NativeParameterGroup
{
  NativeParameterGroup() : index_(-1), value_(), attributes_() {}
  int64_t index_;
  common::NativeTypedDatum value_;
  common::DatumParameterMetadata attributes_;
};

// The parameter owner has an 8-byte word for every slot. Signed BIGINT uses
// that word directly; supported generic values own their descriptor separately.
// State and EvalInfo are disjoint from the payload region. A Datum projection
// exists only after an actual generic Expr ABI request for that slot.
class ParameterValuePage
{
public:
  static int allocate(common::ObIAllocator &owner, int64_t capacity, ParameterValuePage *&out);
  static ParameterValuePage &from_handle(char *handle)
  { return *reinterpret_cast<ParameterValuePage *>(handle); }
  static const ParameterValuePage &from_handle(const char *handle)
  { return *reinterpret_cast<const ParameterValuePage *>(handle); }
  int64_t capacity() const { return capacity_; }
  static int64_t payload_bytes(int64_t capacity) { return capacity * sizeof(uint64_t); }
  static int64_t allocation_bytes(int64_t capacity)
  { return (eval_offset(capacity) + capacity * sizeof(ObEvalInfo) + alignof(uint64_t) - 1)
      / alignof(uint64_t) * alignof(uint64_t); }
  int assign(int64_t slot, const common::Datum &value, const common::SqlType &type,
             common::ObIAllocator &owner);
  int read(int64_t slot, common::Datum &value) const;
  bool read_i64(int64_t slot, int64_t &value) const;
  bool read_i64(int64_t slot, int64_t &value, common::Datum::State &state) const;
  bool matches_layout(int64_t slot, const common::SqlType &type) const;
  common::Datum::State state(int64_t slot) const;
  ObEvalInfo &eval_info(int64_t slot) const;
  int materialize(int64_t slot, common::ObIAllocator &owner, common::Datum *&value);
  common::Datum *find_materialized(int64_t slot) const;
  int prepare_existing_projections(const ParameterValuePage &source,
                                   common::ObIAllocator &owner);
private:
  friend class ObPhysicalPlanCtx;
  explicit ParameterValuePage(int64_t capacity) : capacity_(capacity), projections_(nullptr) {}
  static int64_t eval_offset(int64_t capacity)
  { return (sizeof(ParameterValuePage) + payload_bytes(capacity) + capacity
          + alignof(ObEvalInfo) - 1) / alignof(ObEvalInfo) * alignof(ObEvalInfo); }
  uint64_t *words() const
  { return reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(const_cast<ParameterValuePage *>(this))
      + sizeof(ParameterValuePage)); }
  uint8_t *states() const
  { return reinterpret_cast<uint8_t *>(words() + capacity_); }
  static const uint8_t INLINE_I64 = 0x80;
  static const uint8_t STATE_MASK = 0x7f;
  int64_t capacity_;
  // Allocated only by the first actual generic ABI consumer. Null entries
  // have no Datum object; values remain stable when other entries are added.
  common::Datum **projections_;
};

struct PartParamIdxArray
{
  PartParamIdxArray()
    : part_id_(-1),
      part_param_idxs_()
  {
  }
  TO_STRING_KV(K_(part_id), K_(part_param_idxs));
  int64_t part_id_;
  ObFixedArray<int64_t, common::ObIAllocator> part_param_idxs_;
};
typedef common::ObFixedArray<PartParamIdxArray, common::ObIAllocator> BatchParamIdxArray;
typedef common::ObFixedArray<ObImplicitCursorInfo, common::ObIAllocator> ImplicitCursorInfoArray;

/* refer to a group of array params
 * values clause using now
 */
struct ObArrayParamGroup {
  OB_UNIS_VERSION(1);
public:
  ObArrayParamGroup() : row_count_(0), column_count_(0), start_param_idx_(0) {}
  ObArrayParamGroup(const int64_t row_cnt, const int64_t param_cnt, const int64_t start_param_idx)
    : row_count_(row_cnt), column_count_(param_cnt), start_param_idx_(start_param_idx) {}
  int64_t row_count_;
  int64_t column_count_;
  int64_t start_param_idx_; // in param store
  TO_STRING_KV(K(row_count_), K(column_count_), K(start_param_idx_));
};

class ObPhysicalPlanCtx
{
  OB_UNIS_VERSION(2);
public:
  explicit ObPhysicalPlanCtx(common::ObIAllocator &allocator);
  virtual ~ObPhysicalPlanCtx();
  void destroy() 
  {
    reset_datum_param_store();
    total_memstore_read_row_count_ = 0;
    total_ssstore_read_row_count_ = 0;
    // Member variables that need to request additional memory 
    // with another allocator should call destroy here.
    subschema_ctx_.destroy();
    all_local_session_vars_.destroy();
  }
  
  inline void set_runtime_schema_version(const int64_t version) { runtime_schema_version_ = version; }
  inline int64_t get_runtime_schema_version() const { return runtime_schema_version_; }
  /**
   * @brief: set the timestamp when the execution of this plan should time out
   * @param: ts_timeout_us [in] the microseconds timeout
   */
  inline void set_timeout_timestamp(const int64_t ts_timeout_us)
  {
    ts_timeout_us_ = ts_timeout_us;
  }
  inline int64_t get_timeout_timestamp() const
  {
    return ts_timeout_us_;
  }
  inline int64_t get_ps_timeout_timestamp() const
  {
    // Storage runs locally. Reserving RPC time can put a short query's
    // storage deadline in the past before its scan has even started.
    return ts_timeout_us_;
  }
  inline int64_t get_trans_timeout_timestamp() const
  {
    return ts_timeout_us_;
  }
  inline bool is_timeout(int64_t *remain_us = NULL) const
  {
    int64_t now = common::ObClockGenerator::getClock();
    if (remain_us != NULL) {
      if (OB_LIKELY(ts_timeout_us_ > 0)) {
        *remain_us = ts_timeout_us_ - now;
      } else {
        *remain_us = INT64_MAX; // no timeout
      }
    }
    return (ts_timeout_us_ > 0 && now > ts_timeout_us_);
  }
  inline bool is_exec_timeout(int64_t *remain_us = NULL) const
  {
    int64_t now = common::ObClockGenerator::getClock();
    int64_t ts_timeout_us = ts_timeout_us_;
    if (remain_us != NULL) {
      if (OB_LIKELY(ts_timeout_us > 0)) {
        *remain_us = ts_timeout_us - now;
      } else {
        *remain_us = INT64_MAX; // no timeout
      }
    }
    return (ts_timeout_us > 0 && now > ts_timeout_us);
  }
  // snapshot timestamp used for transaction set consistency check
  inline int64_t get_tsc_timestamp() const
  {
    return tsc_snapshot_timestamp_;
  }
  inline void set_tsc_timestamp(int64_t ts)
  {
    tsc_snapshot_timestamp_ = ts;
  }
  inline void set_consistency_level(const common::ObConsistencyLevel consistency)
  {
    consistency_level_ = consistency;
  }
  inline common::ObConsistencyLevel get_consistency_level() const
  {
    return consistency_level_;
  }
  bool check_consistency_level_validation(const bool contain_inner_table)
  {
    bool bool_ret = true;
    if (contain_inner_table) {
      // Statement which contain inner tables should be strong read;
      bool_ret = (consistency_level_ == ObConsistencyLevel::STRONG);
    }
    return bool_ret;
  }
  // Construction inputs are retired after the final execution pages publish.
  int64_t get_parameter_count() const
  { return native_parameters_ready_ ? native_parameter_metadata_.count() : param_store_.count(); }
  bool native_parameters_ready() const { return native_parameters_ready_; }
  const ParamStore &get_parameter_inputs() const
  { OB_ASSERT(!native_parameters_ready_); return param_store_; }
  int reserve_param_space(int64_t param_count);
  int append_parameter_input(const DatumParameter &parameter);
  int set_parameter_input(int64_t index, const DatumParameter &parameter);
  int assign_parameter_inputs(const ParamStore &parameters);
  int rebuild_runtime_parameters(const ParamStore &parameters);
  int truncate_runtime_parameters(int64_t count);
  void mark_batch_parameters();
  int build_resolution_inputs(ParamStore &inputs, common::ObIAllocator &statement_owner) const;
  int merge_resolution_parameter_flags(const ParamStore &inputs);
  // The callback can edit only staged construction values. Neither it nor a
  // source reference may escape into an initialized execution binding.
  template <typename Writer> int edit_parameter_inputs(Writer writer)
  {
    int ret = native_parameters_ready_ ? common::OB_STATE_NOT_MATCH : common::OB_SUCCESS;
    ParamStore staged((common::ObWrapperAllocator(&parameter_input_allocator_)));
    for (int64_t i = 0; ret == common::OB_SUCCESS && i < param_store_.count(); ++i) {
      DatumParameter copy;
      ret = clone_construction_parameter(param_store_.at(i), copy);
      if (ret == common::OB_SUCCESS) { ret = staged.push_back(copy); }
    }
    if (ret == common::OB_SUCCESS) { ret = writer(staged); }
    if (ret == common::OB_SUCCESS) { ret = assign_parameter_inputs(staged); }
    return ret;
  }
  // Opaque ParameterValuePage handles; these are not Datum frame bytes.
  const ObIArray<char *> &get_param_frame_ptrs() const { return param_frame_ptrs_; }
  int init_datum_param_store();
  // Borrowed outputs name the current effective slot, including a selected
  // group leaf. A retaining consumer must clone before any binding mutation.
  const common::DatumParameterMetadata *get_parameter_metadata(int64_t index) const;
  bool has_native_parameter(int64_t index) const;
  bool read_native_parameter_state(int64_t index, common::Datum::State &state) const;
  int read_native_parameter(int64_t index, common::Datum &value) const;
  int materialize_parameter_for_expr(int64_t index, common::Datum *&value);
  common::Datum *locate_materialized_parameter(int64_t index) const;
  ObEvalInfo *get_parameter_eval_info(int64_t index) const;
  const common::SqlType *get_native_parameter_type(int64_t index) const;
  // Read a present signed BIGINT without exposing its execution slot.
  // A rejected binding leaves value unchanged.
  bool read_native_bigint_parameter(int64_t index, int64_t &value) const;
  // Read one validated signed BIGINT binding, including a typed SQL NULL,
  // through a single page lookup. Failure leaves both outputs unchanged.
  bool read_native_bigint_parameter(
      int64_t index, int64_t &value, common::Datum::State &state) const;
  const common::DatumParameter *get_group_parameter_input(int64_t index) const;
  int capture_native_parameter(int64_t index, common::NativeTypedDatum &value,
      common::DatumParameterMetadata &attributes) const;
  // Metadata contains no value. FullType is supplied by the actual producer;
  // all fallible preparation precedes publication of value/type/attributes.
  int set_runtime_parameter(int64_t index, const common::Datum &value,
      const common::SqlType &supplied_type, const common::DatumParameterMetadata &attributes);
  int clone_runtime_parameters_to(ObPhysicalPlanCtx &destination) const;
  // Capture/validation never evaluate, allocate or request a Datum projection.
  int capture_runtime_parameter_slot(int64_t index, NativeParameterSlotSnapshot &snapshot) const;
  int validate_runtime_parameter_slot(const NativeParameterSlotSnapshot &snapshot) const;
  // Call only after every token in the holder has been validated. No failure or
  // allocation below publication, and current EvalInfo remains unchanged.
  void restore_runtime_parameter_slot(const NativeParameterSlotSnapshot &snapshot);
  int64_t get_native_group_count() const { return native_group_inputs_.count(); }
  int capture_native_group(int64_t group_index, NativeParameterGroup &output) const;
  int runtime_parameter_legacy_size(int64_t index, int64_t &bytes) const;
  int export_runtime_parameter_legacy(int64_t index, common::ObDatum &out,
      void *caller_scratch, int64_t capacity) const;
  // Receiver construction metadata and sparse array inputs are consumed only
  // after the typed PX stream has produced every final page and complete Type.
  int restore_native_parameter_frames(const common::ObIArray<char *> &frames,
      const common::ObIArray<const common::SqlType *> &types, int64_t original_count,
      const common::ObIArray<common::DatumParameterMetadata> *metadata = nullptr,
      const common::ObIArray<NativeParameterGroup> *groups = nullptr);
  void reset_datum_param_store()
  {
    native_parameters_ready_ = false;
    ++parameter_snapshot_generation_;
    native_parameter_metadata_.reuse();
    native_parameter_types_.reuse();
    native_group_inputs_.reuse();
    param_frame_ptrs_.reuse();
    param_store_.destroy();
    parameter_input_allocator_.reset();
    original_param_cnt_ = 0;
    param_frame_capacity_ = 0;
  }
  bool is_terminate(int &ret) const;
  void set_cur_time(const int64_t &session_val)
  {
    cur_time_.set_timestamp(session_val);
  }
  void set_cur_time(const int64_t &session_val, const ObSQLSessionInfo &session);
  const StatementTime &get_cur_time() const { return cur_time_; }
  StatementTime &get_cur_time() { return cur_time_; }
  int64_t get_cur_time_tardy_value() const { return cur_time_.get_datetime() + DELTA_TARDY_TIME_US; }
  bool has_cur_time() const { return cur_time_.has_value(); }
  int64_t get_warning_count() const
  {
    return warning_count_;
  }
  void set_warning_count(int64_t warning_count)
  {
    warning_count_ = warning_count;
  }
  inline void add_warning_count(int64_t warning_count)
  {
    warning_count_ += warning_count;
  }
  int64_t get_affected_rows() const
  {
    return affected_rows_;
  }
  void set_affected_rows(int64_t affected_rows)
  {
    affected_rows_ = affected_rows;
  }
  inline void add_affected_rows(int64_t affected_rows)
  {
    affected_rows_ += affected_rows;
  }
  inline void add_total_memstore_read_row_count(int64_t v)
  {
    total_memstore_read_row_count_ += v;
  }
  inline void add_total_ssstore_read_row_count(int64_t v)
  {
    total_ssstore_read_row_count_ += v;
  }
  inline int64_t get_total_memstore_read_row_count()
  {
    return total_memstore_read_row_count_;
  }
  inline int64_t get_total_ssstore_read_row_count()
  {
    return total_ssstore_read_row_count_;
  }
  int64_t get_found_rows() const
  {
    return found_rows_;
  }
  void set_found_rows(int64_t found_rows)
  {
    found_rows_ = found_rows;
  }
  transaction::ObTxParam &get_trans_param() {return trans_param_;}

  inline void set_autoinc_cache_handle(share::CacheHandle *handle)
  {
    autoinc_cache_handle_ = handle;
  }
  inline share::CacheHandle *get_autoinc_cache_handle() const
  {
    return autoinc_cache_handle_;
  }
  inline int set_autoinc_params(common::ObIArray<share::AutoincParam> &autoinc_params)
  {
    return autoinc_params_.assign(autoinc_params);
  }
  inline void set_tablet_autoinc_param(const share::ObTabletAutoincParam &tablet_autoinc_param)
  {
    tablet_autoinc_param_ = tablet_autoinc_param;
  }
  inline common::ObIArray<share::AutoincParam> &get_autoinc_params()
  {
    return autoinc_params_;
  }
  inline share::ObTabletAutoincParam &get_tablet_autoinc_param()
  {
    return tablet_autoinc_param_;
  }
  inline uint64_t calc_last_insert_id_session()
  {
    if (last_insert_id_cur_stmt_ > 0) {
      last_insert_id_session_ = last_insert_id_cur_stmt_;
      last_insert_id_changed_ = true;
    }
    return last_insert_id_session_;
  }
  inline void set_last_insert_id_session(const uint64_t last_insert_id)
  {
    last_insert_id_session_ = last_insert_id;
  }
  inline uint64_t get_last_insert_id_session() const
  {
    return last_insert_id_session_;
  }
  inline uint64_t calc_last_insert_id_to_client()
  {
    if (last_insert_id_changed_) {
      last_insert_id_to_client_ = last_insert_id_session_;
    } else if (last_insert_id_cur_stmt_ != 0) {
      last_insert_id_to_client_ = last_insert_id_cur_stmt_;
    }
    return last_insert_id_to_client_;
  }
  inline void set_last_insert_id_to_client(const uint64_t last_insert_id)
  {
    last_insert_id_to_client_ = last_insert_id;
  }
  inline uint64_t get_last_insert_id_to_client()
  {
    return last_insert_id_to_client_;
  }
  inline void record_last_insert_id_cur_stmt()
  {
    if (0 == last_insert_id_cur_stmt_ && autoinc_id_tmp_ > 0) {
      last_insert_id_cur_stmt_ = autoinc_id_tmp_;
    }
  }
  inline void record_last_insert_id_cur_stmt_for_batch()
  {
    if (autoinc_id_tmp_ > 0) {
      last_insert_id_cur_stmt_ = autoinc_id_tmp_;
    }
  }
  inline void set_last_insert_id_cur_stmt(const uint64_t last_insert_id)
  {
    last_insert_id_cur_stmt_ = last_insert_id;
  }
  inline uint64_t get_last_insert_id_cur_stmt()
  {
    return last_insert_id_cur_stmt_;
  }
  inline void set_autoinc_id_tmp(const uint64_t autoinc_id)
  {
    autoinc_id_tmp_ = autoinc_id;
  }
  inline uint64_t get_autoinc_id_tmp()
  {
    return autoinc_id_tmp_;
  }
  inline void set_autoinc_col_value(const uint64_t autoinc_value)
  {
    autoinc_col_value_ = autoinc_value;
  }
  inline uint64_t get_autoinc_col_value()
  {
    return autoinc_col_value_;
  }
  inline void set_last_insert_id_with_expr(const bool with_expr)
  {
    last_insert_id_with_expr_ = with_expr;
  }
  inline bool get_last_insert_id_with_expr()
  {
    return last_insert_id_with_expr_;
  }
  inline void set_last_insert_id_changed(const bool changed)
  {
    last_insert_id_changed_ = changed;
  }
  inline bool get_last_insert_id_changed() const
  {
    return last_insert_id_changed_;
  }
  void set_phy_plan(const ObPhysicalPlan *phy_plan);
  inline const ObPhysicalPlan *get_phy_plan() const
  {
    return phy_plan_;
  }
  inline void set_or_expand_transformed(const bool is_or_expand_transformed)
  {
    is_or_expand_transformed_ = is_or_expand_transformed;
  }
  inline bool get_or_expand_transformed() const
  {
    return is_or_expand_transformed_;
  }
  inline void set_check_pdml_affected_rows(const bool check_pdml_affected_rows)
  {
    check_pdml_affected_rows_ = check_pdml_affected_rows;
  }
  inline bool get_check_pdml_affected_rows() const
  {
    return check_pdml_affected_rows_;
  }

  /*
  ** Currently OB has statement-level retry and intra-statement retry, when we encounter error code OB_TRANSACTION_SET_VIOLATION,
  ** we will not re-execute the sql, but re-execute the plan, at this time some variables in plan_ctx need to be reset, currently we have identified only the following types,
  ** when adding new variables, we need to consider whether it will affect the plan re-execution
  */
  inline void reset_for_quick_retry()
  {
    affected_rows_ = 0;
    row_matched_count_ = 0;
    row_duplicated_count_ = 0;
    row_deleted_count_ = 0;
    warning_count_ = 0;
  }
  inline void set_exec_ctx(const ObExecContext *exec_ctx) { exec_ctx_ = exec_ctx; }
  void set_error_ignored(bool ignored) { is_error_ignored_ = ignored; }
  bool is_error_ignored() const { return is_error_ignored_; }
  void set_select_into(bool is_select_into) { is_select_into_  = is_select_into; }
  bool is_select_into() const { return is_select_into_; }
  void set_is_result_accurate(bool is_accurate) { is_result_accurate_ = is_accurate; }
  bool is_result_accurate() const { return is_result_accurate_; }
  void set_foreign_key_checks(bool foreign_key_checks) { foreign_key_checks_ = foreign_key_checks; }
  bool need_foreign_key_checks() const { return foreign_key_checks_; }
  inline bool is_affect_found_row() const { return is_affect_found_row_; }
  inline void set_is_affect_found_row(bool is_affect_found_row) { is_affect_found_row_ = is_affect_found_row; }
  int sync_last_value_local();
  int sync_last_value_to_store();
  int set_row_matched_count(int64_t row_count);
  inline void add_row_matched_count(int64_t row_count) { row_matched_count_ += row_count; }
  int64_t get_row_matched_count() const { return row_matched_count_; }
  int set_row_duplicated_count(int64_t row_count);
  inline void add_row_duplicated_count(int64_t row_count) { row_duplicated_count_ += row_count; }
  int64_t get_row_duplicated_count() const { return row_duplicated_count_; }
  int set_row_deleted_count(int64_t row_count);
  inline void add_row_deleted_count(int64_t row_count) { row_deleted_count_ += row_count; }
  int64_t get_row_deleted_count() const { return row_deleted_count_; }
  void set_expr_op_size(int64_t expr_op_size) { expr_op_size_ = expr_op_size; }
  int64_t get_expr_op_size() const { return expr_op_size_; }
  void set_ignore_stmt(bool is_ignore) { is_ignore_stmt_ = is_ignore; }
  bool is_ignore_stmt() const { return is_ignore_stmt_; }
  bool is_plain_select_stmt() const;
  bool can_partition_retry() const;
  bool has_for_update() const;
  int64_t get_ddl_task_id() const;
  ObTableScanStat &get_table_scan_stat()
  {
    return table_scan_stat_;
  }
  void set_table_row_count_list_capacity(uint32_t capacity)
  { table_row_count_list_.set_capacity(capacity); }
  ObIArray<ObTableRowCount> &get_table_row_count_list() { return table_row_count_list_; }
  inline bool &get_is_multi_dml() { return is_multi_dml_; }
  inline const BatchParamIdxArray &get_batched_stmt_param_idxs() const
  { return batched_stmt_param_idxs_; }
  inline int create_implicit_cursor_infos(int64_t cursor_count)
  { return implicit_cursor_infos_.prepare_allocate(cursor_count); }
  const ImplicitCursorInfoArray &get_implicit_cursor_infos() const
  { return implicit_cursor_infos_; }
  int merge_implicit_cursor_info(const ObImplicitCursorInfo &implicit_cursor_info);
  int merge_implicit_cursors(const common::ObIArray<ObImplicitCursorInfo> &implicit_cursors);
  int replace_implicit_cursor_info(const ObImplicitCursorInfo &implicit_cursor_info);
  void set_cur_stmt_id(int64_t cur_stmt_id) { cur_stmt_id_ = cur_stmt_id; }
  int64_t get_cur_stmt_id() const { return cur_stmt_id_; }
  int switch_implicit_cursor();
  void add_px_dml_row_info(const ObPxDmlRowInfo &dml_row_info);
  TO_STRING_KV(K_(runtime_schema_version));
  void set_field_array(const common::ObIArray<common::ObField> *field_array) { field_array_ = field_array; }
  const common::ObIArray<common::ObField> *get_field_array() { return field_array_;}
  void set_is_ps_protocol(const bool is_ps_protocol) { is_ps_protocol_ = is_ps_protocol; }
  bool is_ps_protocol() const { return is_ps_protocol_; }
  void set_original_param_cnt(const int64_t cnt) { original_param_cnt_ = cnt; }
  int64_t get_original_param_cnt() const { return original_param_cnt_; }
  bool is_exec_param_readable() const { return get_parameter_count() > original_param_cnt_; }
  void set_orig_question_mark_cnt(const int64_t cnt) { orig_question_mark_cnt_ = cnt; }
  int64_t get_orig_question_mark_cnt() const { return orig_question_mark_cnt_; }
  void set_ps_fixed_array_index(const common::ObIArray<int64_t> &fixed_array) { ps_fixed_array_index_ = &fixed_array; }
  const common::ObIArray<int64_t> *get_ps_fixed_array_index() const { return ps_fixed_array_index_; }
  void set_plan_start_time(int64_t t) { plan_start_time_ = t; }
  int64_t get_plan_start_time() const { return plan_start_time_; }
  int replace_batch_param_datum(const int64_t cur_group_id, const int64_t start_param, const int64_t param_cnt);
  void set_last_trace_id(const common::ObCurTraceId::TraceId &trace_id)
  {
    last_trace_id_ = trace_id;
  }
  const common::ObCurTraceId::TraceId &get_last_trace_id() const { return last_trace_id_; }
  common::ObCurTraceId::TraceId &get_last_trace_id() { return last_trace_id_; }
  int get_sqludt_meta_by_subschema_id(uint16_t subschema_id, ObSqlUDTMeta &udt_meta) const;
  int get_sqludt_meta_by_subschema_id(uint16_t subschema_id, ObSubSchemaValue &sub_meta) const;
  bool is_subschema_ctx_inited();
  int get_enumset_meta_by_subschema_id(uint16_t subschema_id,
                                       bool is_in_pl,
                                       const ObEnumSetMeta *&meta) const;
  int get_subschema_id_by_udt_id(uint64_t udt_type_id,
                                 uint16_t &subschema_id,
                                 share::schema::ObSchemaGetterGuard *schema_guard = NULL);
  int get_subschema_id_by_collection_elem_type(ObNestedType coll_type,
                                               const ObDataType &elem_type,
                                               uint16_t &subschema_id);
  int get_subschema_id_by_type_string(const ObString &type_string, uint16_t &subschema_id);
  int get_subschema_id_by_type_info(const SqlTypeMeta &obj_meta,
                                    const ObIArray<common::ObString> &type_info,
                                    uint16_t &subschema_id);
  int build_subschema_by_fields(const ColumnsFieldIArray *fields,
                                share::schema::ObSchemaGetterGuard *schema_guard);
  int build_subschema_ctx_by_param_store(share::schema::ObSchemaGetterGuard *schema_guard);
  ObSubSchemaCtx &get_subschema_ctx() { return subschema_ctx_; }
  inline int set_subschema_ctx(ObSubSchemaCtx &subschema_ctx) {
    return subschema_ctx_.assgin(subschema_ctx);
  }
  const ObIArray<ObArrayParamGroup> &get_array_param_groups() const { return array_param_groups_; }
  ObIArray<ObArrayParamGroup> &get_array_param_groups() { return array_param_groups_; }
  int set_all_local_session_vars(ObIArray<ObLocalSessionVar> &all_local_session_vars);
  int get_local_session_vars(int64_t idx, const ObSolidifiedVarsContext *&local_vars);
  bool is_param_datum_frame_inited() const { return native_parameters_ready_; }
private:
  friend class oceanbase::pl::ObPLExecState;
  friend struct NativeExecutionParametersTestPeer;
  uint64_t parameter_snapshot_generation_ = 1;
  // PL variables use their PL domain owner and do not bind SQL parameter pages.
  // This deliberately private access cannot expose an initialized SQL frame.
  ParamStore &pl_variable_store()
  { OB_ASSERT(!native_parameters_ready_); return param_store_; }
  const ParamStore &pl_variable_store() const
  { OB_ASSERT(!native_parameters_ready_); return param_store_; }
  int clone_construction_parameter(const DatumParameter &source, DatumParameter &output);
  int prepare_runtime_parameter(const DatumParameter &source, common::Datum &value,
      const common::SqlType *&type, int64_t index);
  int publish_runtime_parameter(int64_t index, const common::DatumParameterMetadata &attributes,
      const common::Datum &value, const common::SqlType &type);
  int normalize_parameter_metadata(const common::Datum &value, const common::SqlType &type,
      const common::DatumParameterMetadata &source, common::DatumParameterMetadata &output) const;
  int adopt_runtime_parameters(ObPhysicalPlanCtx &staged);
  int validate_parameter_page_rebind(const common::ObIArray<char *> &pages) const;
  void rebind_parameter_page_aliases(const common::ObIArray<char *> &pages);
  int reserve_param_frame(const int64_t capacity);
  int parameter_wire_view(int64_t index, DatumParameter &output,
      void *scratch, int64_t capacity) const;
  int inner_get_subschema_id_by_type_info(const SqlTypeMeta &obj_meta,
                                          const ObIArray<common::ObString> &type_info,
                                          uint16_t &subschema_id) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObPhysicalPlanCtx);
private:
  // Some datetime functions are evaluated during execution, not before execution.
  // sysdate/systimestamp and current_date/current_timestamp/localtimestamp use
  // different timestamps, so we use `cur_time_` for the first category and
  // `cur_time_ + DELTA_TARDY_TIME_US` for the second category.
  static const int64_t DELTA_TARDY_TIME_US = 5;
  common::ObIAllocator &allocator_;
private:
  /**
   * @note these member need serialize
   */
  
  // used for TRANSACTION SET CONSISTENCY check
  int64_t tsc_snapshot_timestamp_;
  // only used when the sql contains fun like current_time
  StatementTime cur_time_; // fixed statement clock
  // execution timeout for this round of execution
  int64_t ts_timeout_us_;
  common::ObConsistencyLevel consistency_level_;
  // This allocator owns construction blocks and edit-created bytes, rather
  // than forwarding free() into the execution arena. Destroy the store before
  // resetting it; surviving data is already in the final execution owner.
  common::ObArenaAllocator parameter_input_allocator_;
  ParamStore param_store_;
  bool native_parameters_ready_;
  struct GroupParameterInput {
    GroupParameterInput() : index_(-1), input_(), type_(nullptr) {}
    int64_t index_;
    common::DatumParameter input_;
    const common::SqlType *type_;
  };
  common::ObSEArray<common::DatumParameterMetadata, 8> native_parameter_metadata_;
  common::ObSEArray<GroupParameterInput, 2> native_group_inputs_;
  // Effective full types of native execution slots (including the selected
  // element of a grouped array input), owned by this context's arena.
  common::ObSEArray<const common::SqlType *, 16> native_parameter_types_;
  common::ObSEArray<char *, 1, common::ModulePageAllocator, true> param_frame_ptrs_;
  // Original param cont for input SQL, used in static engine for param frames allocation.
  // Param store can be divided into three continuous part:
  //
  //  +----original param------+---const folding param---+---exec param---+
  //  |                        |                         |                |
  //  ▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉▉
  //
  // We will allocate new expr frame for each param part.
  int64_t original_param_cnt_;
  int64_t param_frame_capacity_;
  ObSQLMode sql_mode_;
  common::ObFixedArray<share::AutoincParam, common::ObIAllocator> autoinc_params_;
  share::ObTabletAutoincParam tablet_autoinc_param_;
  // Copied from the session into the physical-plan execution context.
  uint64_t last_insert_id_session_;
  // Expression-context capacity carried with the physical plan execution state.
  int64_t expr_op_size_;
  bool is_ignore_stmt_;
  int64_t bind_array_count_;
  // Current index for array binding parameters used by operators and expressions.
  int64_t bind_array_idx_;
  // System and user tables use different schema-version checks. When a statement
  // contains system tables, SQL leaves this value invalid so storage falls back
  // to the table-level check (see ObRelativeTables::check_schema_version).
  int64_t runtime_schema_version_;
  int64_t orig_question_mark_cnt_;
  common::ObCurTraceId::TraceId last_trace_id_;
  int64_t srs_version_;
  ObSEArray<ObArrayParamGroup, 2> array_param_groups_;

private:
  /**
   * @note these member not need serialize
   */
  transaction::ObTxParam trans_param_;
  int64_t affected_rows_;
  //specified this plan whether affect found_row() result
  bool is_affect_found_row_;
  int64_t found_rows_;
  const ObPhysicalPlan *phy_plan_;
  // if current expr is in insert values, this is the expr's index in value row
  int64_t curr_col_index_;
  share::CacheHandle *autoinc_cache_handle_;
  // last_insert_id return to client 
  uint64_t last_insert_id_to_client_;
  // first auto-increment value in current stmt, last_insert_id which record in session
  uint64_t last_insert_id_cur_stmt_;
  // auto-increment value
  uint64_t autoinc_id_tmp_;
  // auto-increment column value
  uint64_t autoinc_col_value_;
  // set if last_insert_id(expr) called
  bool last_insert_id_with_expr_;
  // if updated, last_insert_id should update in session too
  bool last_insert_id_changed_;
  // use for mysql_info
  int64_t row_matched_count_;
  // duplicated or changed
  int64_t row_duplicated_count_;
  int64_t row_deleted_count_;
  int64_t warning_count_;
  bool is_error_ignored_;
  bool is_select_into_;
  bool is_result_accurate_;
  bool foreign_key_checks_;
  const ObExecContext *exec_ctx_;
  ObTableScanStat table_scan_stat_;
  common::ObFixedArray<ObTableRowCount, common::ObIAllocator> table_row_count_list_; // (table_id, table_row_count) pairs
  // batched_stmt_param_idxs_ stores each partition id corresponding param index
  BatchParamIdxArray batched_stmt_param_idxs_;
  // For multiple statements executed within the same execution plan, the affected row information etc. in the physical plan ctx is a total sum information
  // Need to record the cursor info for each statement executed
  // implicit cursor info array's index is the statement's batch index
  //return to result set, don't need to serialize
  ImplicitCursorInfoArray implicit_cursor_infos_;
  //implicit cursor current stmt id
  int64_t cur_stmt_id_;

  bool is_or_expand_transformed_;
  /*
  ** This variable is used for performance optimization of multi_dml, for the multi_dml plan,
  ** serialization accounts for a large proportion, analysis of the flame graph found that the ParamStore structure occupies the largest portion (mainly insert), but for
  ** multi_dml, ParamStore does not need to be serialized, so this variable controls the serialization behavior of ParamStore
  */
  bool is_multi_dml_;

  //used for expr output pack, do encode according to its field
  const common::ObIArray<ObField> *field_array_;
  //used for expr output pack, do binary encode or text encode
  bool is_ps_protocol_;
  //used for monitor operator information
  int64_t plan_start_time_;
  const common::ObIArray<int64_t> *ps_fixed_array_index_;
  ObSubSchemaCtx subschema_ctx_;
  // for dependant exprs of generated columns
  common::ObFixedArray<ObSolidifiedVarsContext, common::ObIAllocator> all_local_session_vars_;
  int64_t total_memstore_read_row_count_;
  int64_t total_ssstore_read_row_count_;
  bool check_pdml_affected_rows_; // now only worked for pdml checking affected_rows
};

}
}
#endif //OCEANBASE_SQL_OB_PHYSICAL_PLAN_CTX_H
