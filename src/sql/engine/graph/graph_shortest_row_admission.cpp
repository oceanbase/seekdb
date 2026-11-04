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
#include "sql/engine/graph/graph_shortest_row_admission.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
int GraphShortestRowAdmission::init(int64_t memory_limit, const GraphPathDesc &path,
                                   const GraphFeedbackRowDesc &row)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_FAIL(allocator_.init(memory_limit))) {
  } else {
    path_ = path;
    row_ = row;
  }
  return ret;
}

void GraphShortestRowAdmission::reset()
{
  states_.reset();
  sources_.reset();
  keys_.reset();
  allocator_.clear_limit_exceeded();
}

int GraphShortestRowAdmission::materialize_key(
    const ObIArray<ObExpr *> &row, ObEvalCtx &ctx, int64_t begin, int64_t count,
    ObObj *cells, GraphElementIdentity &identity)
{
  int ret = OB_SUCCESS;
  if (begin < 0 || count <= 0 || count > OB_USER_MAX_ROWKEY_COLUMN_NUMBER
      || begin > row.count() - count) {
    ret = OB_ERR_UNEXPECTED;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    ObExpr *expr = row.at(begin + i);
    ObDatum *datum = nullptr;
    if (OB_ISNULL(expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(expr->eval(ctx, datum))) {
    } else if (OB_ISNULL(datum) || datum->is_null()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(datum->to_obj(cells[i], expr->obj_meta_, expr->obj_datum_map_))) {
    }
  }
  if (OB_SUCC(ret)) {
    identity.graph_id_ = path_.graph_id_;
    identity.element_id_ = path_.source_element_id_;
    identity.rowkey_.assign(cells, count);
  }
  return ret;
}

int GraphShortestRowAdmission::intern(GraphIdentityIndex &index,
    const GraphElementIdentity &identity, int64_t binding, int64_t &position, bool &inserted)
{
  int ret = OB_SUCCESS;
  bool found = false;
  inserted = false;
  if (index.count() == 0 && OB_FAIL(index.init(0))) {
  } else if (OB_FAIL(index.find(identity, position, found, binding))) {
  } else if (!found) {
    GraphElementIdentity stable = identity;
    if (OB_FAIL(identity.rowkey_.deep_copy(stable.rowkey_, keys_))) {
    } else if (OB_FAIL(index.get_or_insert(stable, position, inserted, binding))) {
    }
  }
  return ret;
}

int GraphShortestRowAdmission::admit(const ObIArray<ObExpr *> &row,
                                    ObEvalCtx &ctx, bool &accepted)
{
  int ret = OB_SUCCESS;
  accepted = false;
  ObObj source_cells[OB_USER_MAX_ROWKEY_COLUMN_NUMBER];
  ObObj current_cells[OB_USER_MAX_ROWKEY_COLUMN_NUMBER];
  GraphElementIdentity source;
  GraphElementIdentity current;
  ObDatum *depth = nullptr;
  int64_t source_id = -1;
  int64_t state_id = -1;
  bool inserted = false;
  if (row.empty() || row.at(0) == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(row.at(0)->eval(ctx, depth))) {
  } else if (OB_ISNULL(depth) || depth->is_null()
             || depth->get_int() < 1 || depth->get_int() > GRAPH_PATH_MAX_HOPS) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(materialize_key(row, ctx, row_.source_key_expr_begin_,
                                    row_.source_key_expr_count_, source_cells, source))) {
  } else if (OB_FAIL(materialize_key(row, ctx, row_.current_key_expr_begin_,
                                    row_.current_key_expr_count_, current_cells, current))) {
  } else if (OB_FAIL(intern(sources_, source, 0, source_id, inserted))) {
  } else if (source_id > (INT64_MAX - GRAPH_PATH_MAX_HOPS) / (GRAPH_PATH_MAX_HOPS + 1)) {
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_FAIL(intern(states_, current,
              source_id * (GRAPH_PATH_MAX_HOPS + 1) + depth->get_int(), state_id, accepted))) {
  }
  if (ret == OB_ALLOCATE_MEMORY_FAILED && allocator_.limit_exceeded()) {
    ret = OB_EXCEED_QUERY_MEM_LIMIT;
  }
  if (OB_SUCCESS != ret) { reset(); }
  return ret;
}
} // namespace sql
} // namespace oceanbase
