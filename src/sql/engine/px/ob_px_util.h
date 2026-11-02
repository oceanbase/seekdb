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

#ifndef __OCEANBASE_SQL_ENGINE_PX_UTIL_H__
#define __OCEANBASE_SQL_ENGINE_PX_UTIL_H__

#include "lib/container/ob_array.h"
#include "query/engine/expr/expr_extra_serialize_info.h"
#include "sql/engine/px/ob_dfo.h"
#include "sql/dtl/ob_dtl_task.h"
#include "sql/dtl/ob_dtl_flow_control.h"
#include "sql/engine/px/ob_granule_iterator_op.h"
#include "sql/engine/px/ob_px_op_size_factor.h"
#include "sql/engine/px/ob_px_basic_info.h"
#include "sql/engine/dml/ob_table_modify_op.h"
#include "sql/engine/ob_engine_op_traits.h"

namespace oceanbase
{
namespace sql
{
const int64_t PX_RESCAN_BATCH_ROW_COUNT = 8192;
// Listen to various events, such as root dfo scheduling events, etc
class ObIPxCoordEventListener
{
public:
  virtual int on_root_data_channel_setup() = 0;
};


class ObBaseOrderMap
{
public:
  struct ClearMapFunc
  {
    int operator()(const hash::HashMapPair<int64_t, std::pair<ObIArray<int64_t> *, bool>> &entry) {
      entry.second.first->destroy();
      return OB_SUCCESS;
    }
  };
  ObBaseOrderMap() {
  }
  ~ObBaseOrderMap();
  int init(int64_t count);
  inline hash::ObHashMap<int64_t, std::pair<ObIArray<int64_t> *, bool>, hash::NoPthreadDefendMode> &get_map()
  {
    return map_;
  }
  int add_base_partition_order(int64_t pwj_group_id, const TabletIdArray &tablet_id_array,
                               const DASRuntimeTabletIArray &dst_accesses, bool asc);
  int reorder_partition_as_base_order(int64_t pwj_group_id,
                                      const TabletIdArray &tablet_id_array,
                                      DASRuntimeTabletIArray &dst_accesses);
private:
  ObArenaAllocator allocator_;
  hash::ObHashMap<int64_t, std::pair<ObIArray<int64_t> *, bool>, hash::NoPthreadDefendMode> map_;
};

class ObPxSqcUtil
{
public:
  static double get_sqc_partition_ratio(ObExecContext *exec_ctx);
  static double get_sqc_est_worker_ratio(ObExecContext *exec_ctx);


  static int64_t get_total_partition_count(ObExecContext *exec_ctx);
  static int64_t get_sqc_total_partition_count(ObExecContext *exec_ctx);

  static int64_t get_actual_total_worker_count(ObExecContext *exec_ctx);
  static int64_t get_actual_worker_count(ObExecContext *exec_ctx);

  static uint64_t get_plan_id(ObExecContext *exec_ctx);
  static uint64_t get_exec_id(ObExecContext *exec_ctx);
  static uint64_t get_session_id(ObExecContext *exec_ctx);
};
// Size factors may overlap and are evaluated cumulatively.
class ObPxEstimateSizeUtil
{
public:
  static int get_px_size(ObExecContext *exec_ctx, const PxOpSizeFactor factor,
    const int64_t total_size, int64_t &ret_size);
};

class ObSlaveMapItem
{
public:
  ObSlaveMapItem() : group_id_(0), l_worker_count_(0), r_worker_count_(0),
  l_tablet_id_(OB_INVALID_INDEX_INT64), r_tablet_id_(OB_INVALID_INDEX_INT64)
  {
  }
  ~ObSlaveMapItem() = default;
  int64_t group_id_;
  /*
   *                HASH JOIN(parent worker_count)
   *                      |
   *      ---------------------------------
   *      |                               |
   *     TSC1(l_worker_count_)          TSC2(r_worker_count_)
   *
   */
  int64_t p_worker_count_;
  int64_t l_worker_count_;
  int64_t r_worker_count_;
  int64_t l_tablet_id_;
  int64_t r_tablet_id_;
};


typedef common::hash::ObHashMap<uint64_t, int64_t, common::hash::NoPthreadDefendMode> ObTabletIdxMap;



class ObPxSqcDistributionUtil
{
public:
  ObPxSqcDistributionUtil() = default;
  ~ObPxSqcDistributionUtil() = default;
  static int alloc_by_data_distribution(const ObIArray<TableAccessPlan> *table_access_plans,
                                        ObExecContext &ctx,
                                        ObDfo &dfo);

  static int alloc_by_data_distribution_inner(
      const ObIArray<TableAccessPlan> *table_access_plans,
      ObExecContext &ctx, ObDfo &dfo);
  static int alloc_by_child_distribution(const ObDfo &child,
                                         ObDfo &parent);
  static int alloc_by_temp_child_distribution(ObExecContext &ctx,
                                              ObDfo &child);
  static int alloc_by_temp_child_distribution_inner(ObExecContext &ctx,
                                                    ObDfo &child);
  static int alloc_by_local_distribution(ObExecContext &exec_ctx,
                                         ObDfo &root);
  static int alloc_by_reference_child_distribution(ObDfo &parent);
  static int alloc_distribution_of_reference_child(const ObIArray<TableAccessPlan> *table_access_plans,
                                                   ObExecContext &exec_ctx,
                                                   ObDfo &parent);
  static int find_reference_child(ObDfo &parent, ObDfo *&reference_child);
  static int build_tablet_idx_map(
      const share::schema::ObTableSchema *table_schema,
      ObTabletIdxMap &idx_map);
  static int find_dml_ops(common::ObIArray<const ObTableModifySpec *> &insert_ops,
                          const ObOpSpec &op);
  static int find_scan_ops(common::ObIArray<const ObTableScanSpec *> &scan_ops,
                           const ObOpSpec &op);
  static int check_slave_mapping_access_constraint(ObDfo &child, ObDfo &parent);
  static bool check_build_dfo_with_dml(const ObOpSpec &op);

private:
  static int find_dml_ops_inner(common::ObIArray<const ObTableModifySpec *> &insert_ops,
                             const ObOpSpec &op);
  static int find_scan_ops_inner(common::ObIArray<const ObTableScanSpec *> &scan_ops,
                                 const ObOpSpec &op);

  static int build_tablet_idx_map(
      ObSqlExecutorCtx &task_exec_ctx,
      uint64_t ref_table_id,
      ObTabletIdxMap &idx_map);
  static int reorder_all_partitions(
      int64_t table_ref_id, int64_t ref_table_id, const DASRuntimeTabletList &src_accesses,
      DASRuntimeTabletIArray &scan_accesses, bool asc, ObExecContext &exec_ctx,
      ObBaseOrderMap &base_order_map, int64_t op_id,
      ObIArray<std::pair<int64_t, bool>> &tablet_access_order);
  static int build_dynamic_partition_access_plan(common::ObIArray<const ObTableScanSpec*> &scan_ops,
      const ObIArray<TableAccessPlan> *table_access_plans, ObDfo &dfo);

  static int build_dfo_sqc(ObExecContext &ctx,
                           const DASRuntimeTabletList &accesses,
                           ObDfo &dfo);

  /**
   * Calculate the partition information of all tables involved in the current DFO,
   * and record the partition information in the corresponding SQC.
   * At present, the partition information in a DFO can include:
   * 1. partition information of the tsc corresponding table
   * 2. the partition information of the INSERT/REPLACE corresponding table;
   *      Do not consider the information of the table corresponding to DELETE, UPDATE
   *      (the table corresponding to DELETE or UPDATE must appear in the TSC)
   * TODO: Consider the problem of deduplication of the same partition of the table
   *       corresponding to insert and tsc in the presence of INSERT
   */
  static int set_dfo_tablet_accesses(ObExecContext &ctx,
                                       int64_t base_table_ref_id,
                                       ObDfo &dfo,
                                       common::ObIArray<const ObTableScanSpec *> &scan_ops,
                                       const ObTableModifySpec* dml_op,
                                       DASRuntimeTableAccess *dml_access);
  /**
   * Add the partition information (table_access) involved in the
   * current phy_op to the corresponding SQC access access
   */
  static int set_sqc_tablet_accesses(
      ObExecContext &ctx, int64_t base_table_ref_id, ObDfo &dfo,
      ObBaseOrderMap &base_order_map,
      const DASRuntimeTableAccess *table_access, const ObOpSpec *phy_op,
      ObIArray<std::pair<int64_t, bool>> &tablet_access_order);
  /**
   * Get the access sequence of the partition of the current phy_op,
   * the access sequence of the phy_op partition is determined by
   * the access sequence of the GI of the dfo where it is located:
   * 1. DESC
   * 2. ASC
   * The current phy_op type can only be TSC or INSERT
   */
  static int get_access_partition_order(
    ObDfo &dfo,
    const ObOpSpec *phy_op,
    bool &asc_order);

  /**
   * Recursively query the corresponding GI operator in the DFO
   * from the phy_op to obtain the corresponding GI access sequence;
   * root represents the root op of the current DFO
   */
  static int get_access_partition_order_recursively(
    const ObOpSpec *root,
    const ObOpSpec *phy_op,
    bool &asc_order);


private:
  DISALLOW_COPY_AND_ASSIGN(ObPxSqcDistributionUtil);
};


class ObPxOperatorVisitor
{
public:
  class ApplyFunc
  {
  public:
    virtual int apply(ObExecContext &ctx, const ObOpSpec &input) = 0;
    virtual int reset(const ObOpSpec &input) = 0;
  };
public:
  static int visit(ObExecContext &ctx, const ObOpSpec &root, ApplyFunc &func);
};


class PxTabletAccessUtil
{
public:
  /**
   * get all tables' partition info, and store them to sqc_ctx's partition_array_.
   * we need these infos to start trans.
   * IN        tscs
   * IN        scan_accesses
   * OUT       tablets
   */
  static int get_all_tables_tablets(const common::ObIArray<const ObTableScanSpec*> &scan_ops,
                                    const DASRuntimeTabletIArray &all_accesses,
                                    const common::ObIArray<SqcTabletAccessKey> &scan_access_keys,
                                    SqcTabletAccessKey dml_access_key,
                                    common::ObIArray<DASRuntimeTabletArray> &tablets);
};

class ObPxTreeSerializer
{
public:
  // serialize plan tree for engine3.0
  static int serialize_tree(char *buf,
                            int64_t buf_len,
                            int64_t &pos,
                            const ObOpSpec &root,
                            bool is_fulltree,
                            ObPhyOpSeriCtx *seri_ctx = NULL);
  static int deserialize_tree(const char *buf,
                              int64_t data_len,
                              int64_t &pos,
                              ObPhysicalPlan &phy_plan,
                              ObOpSpec *&root,
                              ObIArray<const ObTableScanSpec *> &tsc_ops);
  static int deserialize_tree(const char *buf,
                              int64_t data_len,
                              int64_t &pos,
                              ObPhysicalPlan &phy_plan,
                              ObOpSpec *&root);
  static int serialize_sub_plan(char *buf,
                                int64_t buf_len,
                                int64_t &pos,
                                const ObOpSpec &root);
  static int deserialize_sub_plan(const char *buf,
                                  int64_t data_len,
                                  int64_t &pos,
                                  ObPhysicalPlan &phy_plan,
                                  ObOpSpec *&op);
  static int serialize_op_input(char *buf,
                                int64_t buf_len,
                                int64_t &pos,
                                const ObOpSpec &op_spec,
                                ObOpKitStore &op_kit_store);
  static int deserialize_op_input(const char *buf,
                                  int64_t buf_len,
                                  int64_t &pos,
                                  ObOpKitStore &op_kit_store);
  static int serialize_op_input_tree(
                              char *buf,
                              int64_t buf_len,
                              int64_t &pos,
                              const ObOpSpec &op_spec,
                              ObOpKitStore &op_kit_store,
                              bool is_fulltree,
                              int32_t &real_input_count);
  static int serialize_op_input_subplan(
                              char *buf,
                              int64_t buf_len,
                              int64_t &pos,
                              const ObOpSpec &op_spec,
                              ObOpKitStore &op_kit_store,
                              bool is_fulltree,
                              int32_t &real_input_count);
  static int64_t get_serialize_op_input_size(
                              const ObOpSpec &op_spec,
                              ObOpKitStore &op_kit_store);
  static int64_t get_serialize_op_input_subplan_size(
                              const ObOpSpec &op_spec,
                              ObOpKitStore &op_kit_store,
                              bool is_fulltree);
  static int64_t get_serialize_op_input_tree_size(
                              const ObOpSpec &op_spec,
                              ObOpKitStore &op_kit_store,
                              bool is_fulltree);

  static int64_t get_sub_plan_serialize_size(const ObOpSpec &root);

  static int64_t get_tree_serialize_size(const ObOpSpec &root, bool is_fulltree,
      ObPhyOpSeriCtx *seri_ctx = NULL);

  struct ExprArrayScope
  {
    ExprArrayScope() : saved_(ObExpr::get_serialize_array()) {}
    // A frame and its following tree/input records share this pointer-index
    // directory. Nested frame helpers restore to this same outer binding.
    explicit ExprArrayScope(const ObExprFrameInfo &info) : ExprArrayScope()
    { ObExpr::get_serialize_array() = &info.rt_exprs_; }
    ~ExprArrayScope() { ObExpr::get_serialize_array() = saved_; }
    ObIArray<ObExpr> *saved_;
  };

  template <bool SERIALIZE_PLAN_PART>
  static int serialize_expr_frame_info(char *buf, int64_t buf_len, int64_t &position,
      ObExecContext &ctx, const ObExprFrameInfo &expr_frame_info)
  {
    int ret = OB_SUCCESS;
    int64_t pos = position;
    ExprArrayScope array_scope;
    ObPhysicalPlanCtx *plan_ctx = ctx.get_physical_plan_ctx();
    if (plan_ctx == nullptr || buf == nullptr || pos < 0 || pos > buf_len) { return OB_INVALID_ARGUMENT; }
    if (SERIALIZE_PLAN_PART) {
      OB_UNIS_ENCODE(expr_frame_info.need_ctx_cnt_);
    }
    ObExprExtraSerializeInfo expr_info;
    expr_info.current_time_ = &plan_ctx->get_cur_time();
    expr_info.last_trace_id_ = &plan_ctx->get_last_trace_id();
    if (OB_SUCC(ret)) { ret = expr_info.serialize(buf, buf_len, pos); }
    if (OB_SUCC(ret) && SERIALIZE_PLAN_PART) {
      const ObIArray<ObExpr> &exprs = expr_frame_info.rt_exprs_;
      const int64_t count = expr_frame_info.is_mark_serialize()
          ? expr_frame_info.ser_expr_marks_.count() : exprs.count();
      ObExpr::get_serialize_array() = &const_cast<ObIArray<ObExpr> &>(exprs);
      if (count < 0 || count > INT32_MAX || count > exprs.count()) { ret = OB_SIZE_OVERFLOW; }
      else if (OB_FAIL(serialization::encode_i32(buf, buf_len, pos, static_cast<int32_t>(count)))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
          const ObExpr &expr = exprs.at(i);
          // A decoded partial directory owns its array, but still contains
          // genuine empty slots. Preserve their envelope on the next PX hop.
          const bool selected = !expr_frame_info.is_mark_serialize()
              || expr_frame_info.ser_expr_marks_.at(i);
          ret = selected && !(expr.type_ == T_INVALID && expr.sql_type_ == nullptr)
              ? expr.serialize(buf, buf_len, pos)
              : ObEmptyExpr::instance().serialize(buf, buf_len, pos);
        }
      }
    }
    const bool original = ctx.get_ori_frame_cnt() != 0 && ctx.get_ori_frames() != nullptr;
    const int64_t frame_count = original ? ctx.get_ori_frame_cnt() : ctx.get_frame_cnt();
    char **frames = original ? ctx.get_ori_frames() : ctx.get_frames();
    OB_UNIS_ENCODE(frame_count);
    if (OB_SUCC(ret)) {
      ret = serialize_frame_info<SERIALIZE_PLAN_PART>(buf, buf_len, pos,
          expr_frame_info.const_frame_, expr_frame_info, frames, frame_count);
    }
    if (OB_SUCC(ret) && SERIALIZE_PLAN_PART) {
      ret = NativeFrameCodec::serialize_metadata(expr_frame_info.param_frame_, buf, buf_len, pos);
    }
    if (OB_SUCC(ret)) { ret = NativeFrameCodec::serialize_parameters(*plan_ctx, buf, buf_len, pos); }
    if (OB_SUCC(ret)) {
      ret = serialize_frame_info<SERIALIZE_PLAN_PART>(buf, buf_len, pos,
          expr_frame_info.dynamic_frame_, expr_frame_info, frames, frame_count);
    }
    if (OB_SUCC(ret)) {
      ret = serialize_frame_info<SERIALIZE_PLAN_PART>(buf, buf_len, pos,
          expr_frame_info.datum_frame_, expr_frame_info, frames, frame_count, true);
    }
    if (OB_SUCC(ret)) { position = pos; }
    return ret;
  }

  template <bool SERIALIZE_PLAN_PART>
  static int serialize_frame_info(char *buf, int64_t buf_len, int64_t &pos,
      const ObIArray<ObFrameInfo> &all_frames, const ObExprFrameInfo &expressions,
      char *const *frames, int64_t frame_count, bool no_ser_data = false)
  {
    return NativeFrameCodec::serialize_group(buf, buf_len, pos, SERIALIZE_PLAN_PART,
        all_frames, expressions, frames, frame_count, no_ser_data);
  }

  template <bool DESERIALIZE_PLAN_PART>
  static int deserialize_expr_frame_info(const char *buf, int64_t data_len, int64_t &position,
      ObExecContext &ctx, const ObExprFrameInfo &expr_frame_info)
  {
    int ret = OB_SUCCESS;
    int64_t pos = position;
    ExprArrayScope array_scope;
    ObPhysicalPlanCtx *plan_ctx = ctx.get_physical_plan_ctx();
    if (plan_ctx == nullptr || buf == nullptr || pos < 0 || pos > data_len) { return OB_INVALID_ARGUMENT; }
    if (DESERIALIZE_PLAN_PART) {
      int64_t need_ctx_cnt = 0;
      OB_UNIS_DECODE(need_ctx_cnt);
      if (OB_SUCC(ret) && need_ctx_cnt < 0) { ret = OB_DESERIALIZE_ERROR; }
      if (OB_SUCC(ret)) { const_cast<ObExprFrameInfo &>(expr_frame_info).need_ctx_cnt_ = need_ctx_cnt; }
    }
    ObExprExtraSerializeInfo expr_info;
    expr_info.current_time_ = &plan_ctx->get_cur_time();
    expr_info.last_trace_id_ = &plan_ctx->get_last_trace_id();
    if (OB_SUCC(ret)) { ret = expr_info.deserialize(buf, data_len, pos); }
    if (OB_SUCC(ret) && DESERIALIZE_PLAN_PART) {
      int32_t count = 0;
      ObIArray<ObExpr> &exprs = const_cast<ObArray<ObExpr> &>(expr_frame_info.rt_exprs_);
      ObExpr::get_serialize_array() = &exprs;
      if (data_len - pos < 4) { ret = OB_DESERIALIZE_ERROR; }
      else if (OB_FAIL(serialization::decode_i32(buf, data_len, pos, &count))) {
      } else if (count < 0 || count > data_len - pos) { ret = OB_DESERIALIZE_ERROR; }
      else if (OB_FAIL(exprs.prepare_allocate(count))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) { ret = exprs.at(i).deserialize(buf, data_len, pos); }
      }
    }
    int64_t frame_count = 0;
    char **frames = nullptr;
    OB_UNIS_DECODE(frame_count);
    if (OB_SUCC(ret) && (frame_count < 0 || frame_count > INT64_MAX / sizeof(char *)
        || frame_count > data_len - pos)) { ret = OB_DESERIALIZE_ERROR; }
    if (OB_SUCC(ret) && frame_count > 0) {
      frames = static_cast<char **>(ctx.get_allocator().alloc(sizeof(char *) * frame_count));
      if (frames == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else { MEMSET(frames, 0, sizeof(char *) * frame_count); }
    }
    ObSEArray<char *, 4> const_pointers;
    ObSEArray<char *, 4> parameter_pages;
    ObSEArray<const common::SqlType *, 16> parameter_types;
    ObSEArray<common::DatumParameterMetadata, 8> parameter_metadata;
    ObSEArray<NativeParameterGroup, 2> parameter_groups;
    int32_t parameter_wire_version = 0;
    int64_t original_count = 0;
    if (OB_SUCC(ret)) {
      ret = deserialize_frame_info<DESERIALIZE_PLAN_PART>(buf, data_len, pos, ctx.get_allocator(),
          expr_frame_info.const_frame_, expr_frame_info, &const_pointers, frames, frame_count);
    }
    if (OB_SUCC(ret) && DESERIALIZE_PLAN_PART) {
      ret = NativeFrameCodec::deserialize_metadata(buf, data_len, pos,
          const_cast<ObExprFrameInfo &>(expr_frame_info).param_frame_, frame_count);
    }
    if (OB_SUCC(ret)) {
      ret = NativeFrameCodec::deserialize_parameters(buf, data_len, pos, ctx.get_allocator(),
          parameter_pages, parameter_types, original_count,
          &parameter_metadata, &parameter_groups, &parameter_wire_version);
    }
    if (OB_SUCC(ret)) {
      ret = NativeFrameCodec::bind_parameter_pages(expr_frame_info.param_frame_, parameter_pages,
          parameter_types.count(), original_count, frames, frame_count, &expr_frame_info,
          DESERIALIZE_PLAN_PART);
    }
    if (OB_SUCC(ret)) {
      ret = deserialize_frame_info<DESERIALIZE_PLAN_PART>(buf, data_len, pos, ctx.get_allocator(),
          expr_frame_info.dynamic_frame_, expr_frame_info, nullptr, frames, frame_count);
    }
    if (OB_SUCC(ret)) {
      ret = deserialize_frame_info<DESERIALIZE_PLAN_PART>(buf, data_len, pos, ctx.get_allocator(),
          expr_frame_info.datum_frame_, expr_frame_info, nullptr, frames, frame_count, true);
    }
    // Prepare all fallible plan-container work before adopting the sole native
    // parameter owner. Any truncated later frame leaves PlanCtx/ctx untouched.
    if (OB_SUCC(ret) && DESERIALIZE_PLAN_PART) {
      ret = const_cast<ObExprFrameInfo &>(expr_frame_info).const_frame_ptrs_.assign(const_pointers);
    }
    if (OB_SUCC(ret)) {
      ret = plan_ctx->restore_native_parameter_frames(parameter_pages, parameter_types, original_count,
          parameter_wire_version >= 2 ? &parameter_metadata : nullptr,
          parameter_wire_version >= 2 ? &parameter_groups : nullptr);
    }
    if (OB_SUCC(ret)) {
      // PlanCtx expands short beyond pages before publishing stable slots.
      // Bind this newly decoded directory to the adopted pages as well.
      for (int64_t i = 0; i < frame_count; ++i) {
        for (int64_t j = 0; j < parameter_pages.count(); ++j) {
          if (frames[i] == parameter_pages.at(j)) {
            frames[i] = plan_ctx->get_param_frame_ptrs().at(j);
            break;
          }
        }
      }
      ctx.set_frames(frames);
      ctx.set_frame_cnt(frame_count);
      position = pos;
    }
    return ret;
  }

  template <bool DESERIALIZE_PLAN_PART>
  static int deserialize_frame_info(const char *buf, int64_t data_len, int64_t &pos,
      ObIAllocator &allocator, const ObIArray<ObFrameInfo> &all_frames,
      const ObExprFrameInfo &expressions, ObIArray<char *> *pointers,
      char **frames, int64_t frame_count, bool no_deser_data = false)
  {
    return NativeFrameCodec::deserialize_group(buf, data_len, pos, allocator,
        DESERIALIZE_PLAN_PART, all_frames, expressions, pointers, frames, frame_count, no_deser_data);
  }

  template <bool SERIALIZE_PLAN_PART>
  static int64_t get_serialize_expr_frame_info_size(ObExecContext &ctx,
      const ObExprFrameInfo &expr_frame_info)
  {
    int ret = OB_SUCCESS;
    int64_t len = 0;
    ExprArrayScope array_scope;
    ObPhysicalPlanCtx *plan_ctx = ctx.get_physical_plan_ctx();
    if (plan_ctx == nullptr) { return 0; }
    if (SERIALIZE_PLAN_PART) { OB_UNIS_ADD_LEN(expr_frame_info.need_ctx_cnt_); }
    ObExprExtraSerializeInfo expr_info;
    expr_info.current_time_ = &plan_ctx->get_cur_time();
    expr_info.last_trace_id_ = &plan_ctx->get_last_trace_id();
    len += expr_info.get_serialize_size();
    if (SERIALIZE_PLAN_PART) {
      const ObIArray<ObExpr> &exprs = expr_frame_info.rt_exprs_;
      const int64_t count = expr_frame_info.is_mark_serialize()
          ? expr_frame_info.ser_expr_marks_.count() : exprs.count();
      ObExpr::get_serialize_array() = &const_cast<ObIArray<ObExpr> &>(exprs);
      if (count < 0 || count > INT32_MAX || count > exprs.count()) { ret = OB_SIZE_OVERFLOW; }
      else {
        len += serialization::encoded_length_i32(static_cast<int32_t>(count));
        for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
          const ObExpr &expr = exprs.at(i);
          const bool selected = !expr_frame_info.is_mark_serialize()
              || expr_frame_info.ser_expr_marks_.at(i);
          const int64_t bytes = selected && !(expr.type_ == T_INVALID && expr.sql_type_ == nullptr)
              ? expr.get_serialize_size() : ObEmptyExpr::instance().get_serialize_size();
          if (bytes < 0 || __builtin_add_overflow(len, bytes, &len)) { ret = OB_SIZE_OVERFLOW; }
        }
      }
    }
    const bool original = ctx.get_ori_frame_cnt() != 0 && ctx.get_ori_frames() != nullptr;
    const int64_t frame_count = original ? ctx.get_ori_frame_cnt() : ctx.get_frame_cnt();
    char **frames = original ? ctx.get_ori_frames() : ctx.get_frames();
    OB_UNIS_ADD_LEN(frame_count);
    int64_t bytes = 0;
    if (OB_SUCC(ret) && OB_FAIL(NativeFrameCodec::group_size(SERIALIZE_PLAN_PART,
        expr_frame_info.const_frame_, expr_frame_info, frames, frame_count, false, bytes))) {
    } else if (OB_SUCC(ret) && __builtin_add_overflow(len, bytes, &len)) { ret = OB_SIZE_OVERFLOW; }
    if (OB_SUCC(ret) && SERIALIZE_PLAN_PART) {
      if (OB_FAIL(NativeFrameCodec::metadata_size(expr_frame_info.param_frame_, bytes))) {
      } else if (__builtin_add_overflow(len, bytes, &len)) { ret = OB_SIZE_OVERFLOW; }
    }
    if (OB_SUCC(ret) && OB_FAIL(NativeFrameCodec::parameter_size(*plan_ctx, bytes))) {
    } else if (OB_SUCC(ret) && __builtin_add_overflow(len, bytes, &len)) { ret = OB_SIZE_OVERFLOW; }
    if (OB_SUCC(ret) && OB_FAIL(NativeFrameCodec::group_size(SERIALIZE_PLAN_PART,
        expr_frame_info.dynamic_frame_, expr_frame_info, frames, frame_count, false, bytes))) {
    } else if (OB_SUCC(ret) && __builtin_add_overflow(len, bytes, &len)) { ret = OB_SIZE_OVERFLOW; }
    if (OB_SUCC(ret) && OB_FAIL(NativeFrameCodec::group_size(SERIALIZE_PLAN_PART,
        expr_frame_info.datum_frame_, expr_frame_info, frames, frame_count, true, bytes))) {
    } else if (OB_SUCC(ret) && __builtin_add_overflow(len, bytes, &len)) { ret = OB_SIZE_OVERFLOW; }
    if (OB_FAIL(ret)) {
      SQL_LOG(WARN, "native frame has no valid wire size", K(ret));
      len = 0;
    }
    return len;
  }

  template <bool SERIALIZE_PLAN_PART>
  static int64_t get_serialize_frame_info_size(const ObIArray<ObFrameInfo> &all_frames,
      const ObExprFrameInfo &expressions, char *const *frames, int64_t frame_count,
      bool no_ser_data = false)
  {
    int64_t size = 0;
    const int ret = NativeFrameCodec::group_size(SERIALIZE_PLAN_PART,
        all_frames, expressions, frames, frame_count, no_ser_data, size);
    if (ret != OB_SUCCESS) { SQL_LOG(WARN, "native frame group has no valid wire size", K(ret)); }
    return size;
  }

};

class ObPxChannelUtil
{
public:
  static int unlink_ch_set(
             dtl::ObDtlChSet &ch_set, sql::dtl::ObDtlFlowControl *dfc, const bool batch_free_ch);
  static int flush_rows(common::ObIArray<dtl::ObDtlChannel*> &channels);


  // asyn wait
  static int sqcs_channles_asyn_wait(common::ObIArray<sql::ObPxSqcMeta> &sqcs);
};

class ObPxAffinityByRandom
{
public:
  struct TabletHashValue
  {
    int64_t tablet_id_;
    int64_t tablet_idx_;
    uint64_t hash_value_;
    int64_t worker_id_;
    ObPxTabletInfo partition_info_;
    TO_STRING_KV(K_(tablet_id), K_(tablet_idx), K_(hash_value), K_(worker_id), K_(partition_info));
  };
public:
  ObPxAffinityByRandom(bool order_partitions, bool partition_random_affinitize)
      : worker_cnt_(0), tablet_hash_values_(), order_partitions_(order_partitions),
        partition_random_affinitize_(partition_random_affinitize)
  {}
  virtual ~ObPxAffinityByRandom() = default;
  int reserve(int64_t size) { return tablet_hash_values_.reserve(size); }
  int add_partition(int64_t tablet_id,
      int64_t tablet_idx,
      int64_t worker_cnt,
      ObPxTabletInfo &partition_row_info);
  int do_random(bool use_partition_info);
  const ObIArray<TabletHashValue> &get_result() { return tablet_hash_values_; }
  static int get_tablet_info(int64_t tablet_id, ObIArray<ObPxTabletInfo> &partitions_info, ObPxTabletInfo &partition_info);
private:
  int64_t worker_cnt_;
  ObSEArray<TabletHashValue, 8> tablet_hash_values_;
  bool order_partitions_;
  bool partition_random_affinitize_;// whether do partition random in gi task split
};

class ObSlaveMapUtil
{
public:
  ObSlaveMapUtil() = default;
  ~ObSlaveMapUtil() = default;

  // build_slave_mapping_mn_ch_map and build_pkey_mn_ch_map will both build channel map
  // and partition map
  static int build_slave_mapping_mn_ch_map(ObExecContext &ctx,
                                           ObDfo &child,
                                           ObDfo &parent);
  static int build_pkey_mn_ch_map(ObExecContext &ctx,
                                  ObDfo &child,
                                  ObDfo &parent);
  // build channel map
  static int build_mn_channel(ObPxChTotalInfos *dfo_ch_total_infos,
                              ObDfo &child,
                              ObDfo &parent);
private:
  // ----------------- for slave mapping scenes ----------------------
  // for SlaveMappingType::SM_PWJ_HASH_HASH, channel built inside each sqc
  static int build_pwj_slave_map_mn_group(ObDfo &parent, ObDfo &child);
  // for SlaveMappingType::SM_PPWJ_HASH_HASH
  static int build_ppwj_slave_mn_map(ObDfo &parent, ObDfo &child);

  // for SlaveMappingType::SM_PPWJ_BCAST_NONE && SlaveMappingType::SM_PPWJ_NONE_BCAST
  static int build_ppwj_bcast_slave_mn_map(ObDfo &parent, ObDfo &child);



  // ----------------- for normal pkey -----------------
  // for child with ObPQDistributeMethod::Type::PARTITION
  static int build_ppwj_ch_mn_map(ObExecContext &ctx, ObDfo &parent, ObDfo &child);



  // ----------------- for pdml -------------------------------------
  // for child with ObPQDistributeMethod::Type::PARTITION_RANDOM
  static int build_pkey_random_ch_mn_map(ObDfo &parent, ObDfo &child);

  // for child with ObPQDistributeMethod::Type::PARTITION_HASH or PARTITION_RANGE
  static int build_pkey_affinitized_ch_mn_map(ObDfo &parent, ObDfo &child);
  static int build_affinitized_partition_map_by_sqcs(common::ObIArray<ObPxSqcMeta> &sqcs,
                                                     ObDfo &child,
                                                     ObPxPartChMapArray &map);

private:
  static int build_partition_map_by_sqcs(common::ObIArray<ObPxSqcMeta> &sqcs,
                                         ObDfo &child,
                                         ObPxPartChMapArray &map);

  static int get_pkey_tablet_accesses(int64_t table_ref_id,
      ObPxSqcMeta &sqc,
      DASRuntimeTabletIArray &pkey_accesses);
};

using DTLChannelPredFunc = std::function<void(dtl::ObDtlChannel*)>;

class ObDtlChannelUtil
{
public:
  static int get_receive_dtl_channel_set(
              const int64_t sqc_id,
              const int64_t task_id,
              dtl::ObDtlChTotalInfo &ch_total_info,
              dtl::ObDtlChSet &ch_set);
  static int get_transmit_dtl_channel_set(
              const int64_t sqc_id,
              const int64_t task_id,
              dtl::ObDtlChTotalInfo &ch_total_info,
              dtl::ObDtlChSet &ch_set);
};


class ObVirtualTableErrorWhitelist
{
public:
  static bool should_ignore_vtable_error(int error_code);
};

class ObPxErrorUtil
{
public:
  static inline void update_qc_error_code(int &current_error_code,
                                           const int new_error_code,
                                           const ObPxUserErrorMsg &from)
  {
    // **replace** error code & error msg
    if (new_error_code != ObPxTask::TASK_DEFAULT_RET_VALUE) {
      if ((OB_SUCCESS == current_error_code ||
           OB_ERR_SIGNALED_IN_PARALLEL_QUERY_SERVER == current_error_code ||
           OB_GOT_SIGNAL_ABORTING == current_error_code) &&
           OB_SUCCESS != new_error_code) {
        current_error_code = new_error_code;
        SQL_LOG_RET(WARN, new_error_code, "QC updated the local PX error code", K(new_error_code));
        FORWARD_USER_ERROR(new_error_code, from.msg_);
      }
    }
    // **append** warning msg
    for (int i = 0; i < from.warnings_.count(); ++i) {
      const common::ObWarningBuffer::WarningItem &warning_item = from.warnings_.at(i);
      if (USER_WARN == warning_item.log_level_) {
        FORWARD_USER_WARN(warning_item.code_, warning_item.msg_);
      } else if (USER_NOTE == warning_item.log_level_) {
        FORWARD_USER_NOTE(warning_item.code_, warning_item.msg_);
      }
    }
  }

  static inline void update_sqc_error_code(int &current_error_code,
                                           const int new_error_code,
                                           const ObPxUserErrorMsg &from,
                                           ObPxUserErrorMsg &to)
  {
    int ret = OB_SUCCESS;
    // **replace** error code & error msg
    if (new_error_code != ObPxTask::TASK_DEFAULT_RET_VALUE) {
      if ((OB_SUCCESS == current_error_code) ||
          ((OB_ERR_SIGNALED_IN_PARALLEL_QUERY_SERVER == current_error_code ||
            OB_GOT_SIGNAL_ABORTING == current_error_code) &&
           OB_SUCCESS != new_error_code)) {
        current_error_code = new_error_code;
        (void)snprintf(to.msg_, common::OB_MAX_ERROR_MSG_LEN, "%s", from.msg_);
      }
    }
    // **append** warning msg
    for (int i = 0; i < from.warnings_.count(); ++i) {
      if (OB_FAIL(to.warnings_.push_back(from.warnings_.at(i)))) {
      }
    }
  }

  //update the error code if it is OB_HASH_NOT_EXIST or OB_ERR_SIGNALED_IN_PARALLEL_QUERY_SERVER
  static inline void update_error_code(int &current_error_code, const int new_error_code)
  {
    if (new_error_code != ObPxTask::TASK_DEFAULT_RET_VALUE) {
      if ((OB_SUCCESS == current_error_code) ||
          ((OB_ERR_SIGNALED_IN_PARALLEL_QUERY_SERVER == current_error_code ||
            OB_GOT_SIGNAL_ABORTING == current_error_code) &&
           OB_SUCCESS != new_error_code)) {
        current_error_code = new_error_code;
      }
    }
  }
};

class LowestCommonAncestorFinder
{
public:
  static int find_op_common_ancestor(
      const ObOpSpec *left, const ObOpSpec *right, const ObOpSpec *&ancestor);
  static int get_op_dfo(const ObOpSpec *op, ObDfo *root_dfo, ObDfo *&op_dfo);
};

}
}

#endif /* __OCEANBASE_SQL_ENGINE_PX_UTIL_H__ */
//// end of header file
