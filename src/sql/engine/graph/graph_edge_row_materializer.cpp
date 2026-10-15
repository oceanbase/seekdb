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
#include "sql/engine/graph/graph_edge_row_materializer.h"
#include "sql/engine/graph/graph_das_access_utils.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "share/ob_json_access_utils.h"
#include "lib/utility/ob_smart_call.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
namespace
{

int clear_payload_eval_flags(ObExpr &expr, ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  // DAS owns column datums. Only invalidate calculated values above those
  // leaves; nested JSON_OBJECT/CAST expressions must not reuse the last edge.
  if (expr.arg_cnt_ > 0) {
    expr.clear_evaluated_flag(ctx);
    for (uint32_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      if (OB_ISNULL(expr.args_[i])) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(SMART_CALL(clear_payload_eval_flags(*expr.args_[i], ctx)))) {
      }
    }
  }
  return ret;
}

int bind_column_exprs(const ObDASScanCtDef &ctdef,
                      const uint64_t *column_ids,
                      int64_t column_count,
                      ObExpr **exprs)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(column_ids == nullptr || exprs == nullptr
                  || column_count <= 0
                  || column_count > OB_USER_MAX_ROWKEY_COLUMN_NUMBER)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(ctdef.access_column_ids_.count()
                         != ctdef.pd_expr_spec_.access_exprs_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("graph edge DAS access columns and expressions do not match",
             K(ret), K(ctdef.access_column_ids_.count()),
             K(ctdef.pd_expr_spec_.access_exprs_.count()));
  }
  for (int64_t key = 0; OB_SUCC(ret) && key < column_count; ++key) {
    for (int64_t column = 0;
         exprs[key] == nullptr && column < ctdef.access_column_ids_.count();
         ++column) {
      if (ctdef.access_column_ids_.at(column) == column_ids[key]) {
        exprs[key] = ctdef.pd_expr_spec_.access_exprs_.at(column);
      }
    }
    if (OB_ISNULL(exprs[key])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("graph edge identity column is absent from DAS output", K(ret),
               K(key), K(column_ids[key]), K(ctdef.ref_table_id_));
    }
  }
  return ret;
}
} // namespace


int GraphIdentityExprBinding::init(
    const ObDASScanCtDef &ctdef, const uint64_t *columns, int64_t count)
{
  int ret = OB_SUCCESS;
  GraphIdentityExprBinding candidate;
  if (OB_FAIL(bind_column_exprs(ctdef, columns, count, candidate.key_exprs_))) {
  } else {
    candidate.key_count_ = count;
    if (OB_UNLIKELY(!candidate.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      *this = candidate;
    }
  }
  return ret;
}

bool GraphIdentityExprBinding::is_valid() const
{
  bool valid = key_count_ > 0 && key_count_ <= OB_USER_MAX_ROWKEY_COLUMN_NUMBER;
  for (int64_t i = 0; valid && i < key_count_; ++i) {
    valid = key_exprs_[i] != nullptr;
  }
  return valid;
}

int GraphEdgeRowMaterializer::materialize_identity(
    uint64_t element_id, const GraphIdentityExprBinding &binding,
    GraphElementIdentity &identity)
{
  return graph_das_detail::materialize_identity(
      eval_ctx_, allocator_, path_desc_.graph_id_, element_id,
      binding.key_count_, binding.key_exprs_, identity);
}

int GraphEdgeRowMaterializer::row_has_null_endpoint(
    const GraphIdentityExprBinding &source,
    const GraphIdentityExprBinding &target, bool &has_null) const
{
  int ret = OB_SUCCESS;
  has_null = false;
  for (int endpoint = 0; OB_SUCC(ret) && !has_null && endpoint < 2;
       ++endpoint) {
    const int64_t key_count = endpoint == 0 ? source.key_count_
                                            : target.key_count_;
    ObExpr *const *key_exprs = endpoint == 0 ? source.key_exprs_
                                              : target.key_exprs_;
    for (int64_t key = 0; OB_SUCC(ret) && !has_null && key < key_count; ++key) {
      ObDatum *datum = nullptr;
      if (OB_ISNULL(key_exprs[key])) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(key_exprs[key]->eval(eval_ctx_, datum))) {
      } else if (OB_ISNULL(datum)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        has_null = datum->is_null();
      }
    }
  }
  return ret;
}

int GraphEdgeRowMaterializer::materialize_edge(
    const GraphIdentityExprBinding &source,
    const GraphIdentityExprBinding &edge_binding,
    const GraphIdentityExprBinding &target,
    GraphExpandEdge &edge, bool &matches)
{
  int ret = OB_SUCCESS;
  bool has_null_endpoint = false;
  edge = GraphExpandEdge();
  matches = false;
  if (OB_UNLIKELY(!source.is_valid() || !edge_binding.is_valid() || !target.is_valid())) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(row_has_null_endpoint(source, target, has_null_endpoint))) {
  } else if (has_null_endpoint) {
    // SQL equality does not join a NULL endpoint to a frontier or vertex key,
    // so this physical row is not a graph-edge match.
  } else if (OB_FAIL(materialize_identity(
                 path_desc_.source_element_id_, source, edge.source_identity_))) {
  } else if (OB_FAIL(materialize_identity(
                 path_desc_.edge_element_id_, edge_binding, edge.edge_identity_))) {
  } else if (OB_FAIL(materialize_identity(
                 path_desc_.target_element_id_, target, edge.target_identity_))) {
  } else {
    matches = true;
  }
  return ret;
}

int GraphEdgeRowMaterializer::materialize_payload(
    const GraphExpandScanDesc &scan_desc, GraphEdgePayload &payload)
{
  int ret = OB_SUCCESS;
  const GraphExpandScanDesc *desc = &scan_desc;
  if (!desc->payload_exprs_.empty()) {
    MultimodeAlloctor json_allocator(allocator_);
    const int64_t count = desc->payload_exprs_.count();
    ObObj *cells = static_cast<ObObj *>(allocator_.alloc(sizeof(ObObj) * count));
    if (OB_ISNULL(cells)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      new(cells) ObObj[count];
      payload.values_.assign(cells, count);
      for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
        ObExpr *append_expr = desc->payload_exprs_.at(i);
        ObIJsonBase *value = nullptr;
        ObString binary;
        if (OB_ISNULL(append_expr) || append_expr->arg_cnt_ != 3
            || OB_ISNULL(append_expr->args_[2])) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(clear_payload_eval_flags(*append_expr->args_[2], eval_ctx_))) {
        } else if (OB_FAIL(ObJsonExprHelper::get_json_val(
                       *append_expr, eval_ctx_, &json_allocator, 2, value))) {
        } else if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(
                       value, binary, &allocator_))) {
        } else {
          // This is one encoded JSON value, including SQL NULL as JSON null.
          // It is not a serialized identity used for graph equality.
          cells[i].set_varbinary(binary);
        }
      }
    }
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
