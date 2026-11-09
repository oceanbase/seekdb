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

#pragma once

#include "sql/engine/graph/graph_expand_access_desc.h"

namespace oceanbase
{
namespace sql
{
// Positional layout of the generated recursive row consumed by the native
// feedback runtime. It deliberately uses expression indexes rather than the
// temporary __g_* aliases created by the SQL lowering.
struct GraphFeedbackRowDesc
{
  OB_UNIS_VERSION(1);
public:
  int init(const GraphPathDesc &path_desc,
           const GraphExpandAccessDesc &access_desc,
           int64_t output_count);
  bool is_valid(const GraphPathDesc &path_desc,
                const GraphExpandAccessDesc &access_desc) const;

  int64_t depth_expr_index_{0};
  int64_t source_key_expr_begin_{0};
  int64_t source_key_expr_count_{0};
  int64_t current_key_expr_begin_{0};
  int64_t current_key_expr_count_{0};
  int64_t trail_key_expr_begin_{0};
  int64_t trail_key_expr_count_{0};
  // Optional JSON edge-identity/property arrays follow the fixed fields.
  int64_t payload_expr_begin_{0};
  int64_t payload_expr_count_{0};
  int64_t output_expr_count_{0};
  TO_STRING_KV(K_(depth_expr_index), K_(source_key_expr_begin),
               K_(source_key_expr_count), K_(current_key_expr_begin),
               K_(current_key_expr_count), K_(trail_key_expr_begin),
               K_(trail_key_expr_count), K_(payload_expr_begin),
               K_(payload_expr_count), K_(output_expr_count));
};

// Native feedback materializes hop count, source/current keys and supported
// ordered edge-value arrays in the generated recursive row. Keep this predicate
// shared by EXPLAIN and execution so a plan never advertises a runtime that the
// physical operator will reject.
bool supports_basic_native_graph_feedback(
    const GraphPathDesc &path_desc,
    const GraphExpandAccessDesc &access_desc,
    int64_t output_expr_count,
    int64_t payload_expr_count = 0);


} // namespace sql
} // namespace oceanbase
