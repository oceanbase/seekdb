/*
 * Copyright (c) 2026 OceanBase.
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
#include "sql/engine/graph/graph_feedback_output.h"
#include "sql/engine/graph/graph_feedback_loop_op.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "share/ob_errno.h"
#include "share/ob_json_access_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

int GraphFeedbackRowMaterializer::write_value(ObExpr *expr,
                                               const ObObj &value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph feedback output expression is null", K(ret));
  } else {
    ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
    if (OB_FAIL(datum.from_obj(value, expr->obj_datum_map_))) {
      LOG_WARN("failed to write graph feedback output value", K(ret),
               K(value), KPC(expr));
    } else {
      expr->set_evaluated_projected(eval_ctx_);
      ObEvalInfo &info = expr->get_eval_info(eval_ctx_);
      info.notnull_ = !datum.is_null();
      info.point_to_frame_ = false;
    }
  }
  return ret;
}

int GraphFeedbackRowMaterializer::write_key(
    const ExprFixedArray &output_exprs,
    int64_t begin,
    int64_t count,
    const GraphElementIdentity &identity)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(begin < 0 || count <= 0
                  || begin > output_exprs.count() - count
                  || identity.rowkey_.get_obj_cnt() != count)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph feedback output key shape is inconsistent", K(ret),
             K(begin), K(count), "output_count", output_exprs.count(),
             K(identity));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (OB_FAIL(write_value(
            output_exprs.at(begin + i), identity.rowkey_.get_obj_ptr()[i]))) {
      LOG_WARN("failed to write graph feedback output key", K(ret), K(i),
               K(begin), K(identity));
    }
  }
  return ret;
}

int GraphFeedbackRowMaterializer::materialize_path(
    const GraphFeedbackLoopSpec &spec,
    const ObIArray<GraphPathState> &path)
{
  int ret = OB_SUCCESS;
  ObObj hop_value;
  const GraphFeedbackRowDesc &desc = spec.get_output_row_desc();
  const ExprFixedArray &output_exprs = spec.output_union_exprs_;
  if (OB_UNLIKELY(path.empty()
                  || desc.output_expr_count_ != output_exprs.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid graph feedback output row", K(ret),
             "path_count", path.count(), K(desc),
             "output_count", output_exprs.count());
  } else if (OB_UNLIKELY(!spec.can_use_basic_native_runtime())) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("graph feedback output row is outside basic native capability",
             K(ret), K(spec.get_path_desc()),
             K(spec.get_expand_access_desc()), K(desc));
  } else {
    const GraphPathState &root = path.at(0);
    const GraphPathState &leaf = path.at(path.count() - 1);
    hop_value.set_int(leaf.hop_);
    if (OB_FAIL(write_value(
            output_exprs.at(desc.depth_expr_index_), hop_value))) {
    } else if (OB_FAIL(write_key(
                   output_exprs, desc.source_key_expr_begin_,
                   desc.source_key_expr_count_, root.current_identity_))) {
    } else if (OB_FAIL(write_key(
                   output_exprs, desc.current_key_expr_begin_,
                   desc.current_key_expr_count_, leaf.current_identity_))) {
    } else {
      const int64_t key_count = spec.get_expand_access_desc().edge_key_count_;
      for (int64_t slot = 0; OB_SUCC(ret)
           && slot * key_count < desc.trail_key_expr_count_; ++slot) {
        const int64_t begin = desc.trail_key_expr_begin_ + slot * key_count;
        if (slot + 1 < path.count()) {
          ret = write_key(output_exprs, begin, key_count,
                          path.at(slot + 1).incoming_edge_identity_);
        } else {
          ObObj unused;
          unused.set_null();
          for (int64_t key = 0; OB_SUCC(ret) && key < key_count; ++key) {
            ret = write_value(output_exprs.at(begin + key), unused);
          }
        }
      }
      if (OB_SUCC(ret)) {
        ret = materialize_path_payloads(spec, path);
      }
    }
  }
  return ret;
}

int GraphFeedbackRowMaterializer::materialize_shortest_distance(
    const GraphFeedbackLoopSpec &spec,
    const GraphElementIdentity &source,
    const GraphElementIdentity &goal,
    int64_t distance)
{
  int ret = OB_SUCCESS;
  const GraphFeedbackRowDesc &desc = spec.get_output_row_desc();
  const ExprFixedArray &output_exprs = spec.output_union_exprs_;
  ObObj hop_value;
  hop_value.set_int(distance);
  if (OB_UNLIKELY(desc.output_expr_count_ != output_exprs.count()
                  || desc.trail_key_expr_count_ != 0
                  || desc.payload_expr_count_ != 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("bound shortest row has unsupported output fields", K(ret), K(desc));
  } else if (OB_FAIL(write_value(
                 output_exprs.at(desc.depth_expr_index_), hop_value))) {
  } else if (OB_FAIL(write_key(
                 output_exprs, desc.source_key_expr_begin_,
                 desc.source_key_expr_count_, source))) {
  } else if (OB_FAIL(write_key(
                 output_exprs, desc.current_key_expr_begin_,
                 desc.current_key_expr_count_, goal))) {
  }
  return ret;
}

int GraphFeedbackRowMaterializer::materialize_path_payloads(
    const GraphFeedbackLoopSpec &spec,
    const ObIArray<GraphPathState> &path)
{
  int ret = OB_SUCCESS;
  payload_allocator_.reuse();
  const GraphFeedbackRowDesc &desc = spec.get_output_row_desc();
  for (int64_t column = 0; OB_SUCC(ret)
       && column < desc.payload_expr_count_; ++column) {
    ObJsonArray array(&payload_allocator_);
    // The root has no incoming edge. Walking root-to-leaf preserves order
    // and repeated edges; SQL's existing zero-hop wrapper handles [] output.
    for (int64_t hop = 1; OB_SUCC(ret) && hop < path.count(); ++hop) {
      const ObNewRow &row = path.at(hop).incoming_edge_payload_.values_;
      ObIJsonBase *value = nullptr;
      if (OB_UNLIKELY(row.count_ != desc.payload_expr_count_
                      || row.cells_ == nullptr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ObJsonBaseFactory::get_json_base(
                     &payload_allocator_, row.cells_[column].get_string(),
                     ObJsonInType::JSON_BIN, ObJsonInType::JSON_TREE, value))) {
      } else if (OB_FAIL(array.array_append(value))) {
      }
    }
    ObString binary;
    ObExpr *output = spec.output_union_exprs_.at(
        desc.payload_expr_begin_ + column);
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(output)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (ObJsonExprHelper::is_json_depth_exceed_limit(array.depth())) {
        ret = OB_ERR_JSON_OUT_OF_DEPTH;
      } else if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(
                     &array, binary, &payload_allocator_))) {
      } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(
                     *output, eval_ctx_, output->locate_datum_for_write(eval_ctx_),
                     binary))) {
      } else {
        output->set_evaluated_projected(eval_ctx_);
      }
    }
  }
  return ret;
}

void GraphFeedbackOutputBatch::begin()
{
  rows_.reuse();
  allocator_.reuse();
}

int GraphFeedbackOutputBatch::stage(const ExprFixedArray &output_exprs)
{
  int ret = OB_SUCCESS;
  ObChunkDatumStore::StoredRow *row = nullptr;
  if (OB_FAIL(ObChunkDatumStore::StoredRow::build(
          row, output_exprs, eval_ctx_, allocator_))) {
  } else if (OB_FAIL(rows_.push_back(row))) {
  }
  return ret;
}

int GraphFeedbackOutputBatch::publish(const ExprFixedArray &output_exprs)
{
  int ret = OB_SUCCESS;
  ObEvalCtx::BatchInfoScopeGuard guard(eval_ctx_);
  guard.set_batch_size(rows_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < rows_.count(); ++i) {
    guard.set_batch_idx(i);
    ret = rows_.at(i)->to_expr(output_exprs, eval_ctx_);
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < output_exprs.count(); ++i) {
    ObExpr *expr = output_exprs.at(i);
    expr->set_evaluated_projected(eval_ctx_);
    expr->get_eval_info(eval_ctx_).notnull_ = false;
    expr->get_eval_info(eval_ctx_).point_to_frame_ = false;
  }
  return ret;
}

void GraphFeedbackOutputBatch::reset()
{
  rows_.reset();
  allocator_.reset();
}

} // namespace sql
} // namespace oceanbase
