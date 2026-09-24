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

#include "sql/engine/table/ob_table_scan_op.h"

namespace oceanbase
{
namespace sql
{

// Self-contained physical access description used by GraphExpand. During
// local execution codegen_ctdef_ borrows the already generated table-scan
// ctdef. Plan serialization encodes that ctdef and deserialization rebuilds it
// in owned_ctdef_, so a PX DFO need not find a table-scan operator owned by
// another DFO.
struct GraphExpandScanDesc
{
  OB_UNIS_VERSION(1);
public:
  explicit GraphExpandScanDesc(common::ObIAllocator &allocator);
  ~GraphExpandScanDesc() = default;

  int init(const ObTableScanSpec &scan_spec);
  bool is_valid() const;
  const ObTableScanCtDef &get_tsc_ctdef() const
  {
    return codegen_ctdef_ == nullptr ? owned_ctdef_ : *codegen_ctdef_;
  }
  uint64_t get_table_loc_id() const { return table_loc_id_; }
  uint64_t get_id() const { return scan_op_id_; }

  // scan_op_id_ is retained for DAS diagnostics and runtime statistics only;
  // it is not used to locate an operator at execution time.
  uint64_t scan_op_id_{common::OB_INVALID_ID};
  uint64_t table_loc_id_{common::OB_INVALID_ID};
  uint64_t ref_table_id_{common::OB_INVALID_ID};
  int64_t frozen_version_{0};
  int64_t rows_{0};
  int64_t width_{0};
  bool need_check_output_datum_{false};
  ExprFixedArray startup_filters_;
  ExprFixedArray filters_;
  ObTableScanCtDef owned_ctdef_;
  // Codegen-owned backing used before plan serialization. This pointer is
  // deliberately not serialized; deserialized plans use owned_ctdef_.
  const ObTableScanCtDef *codegen_ctdef_{nullptr};
};

} // namespace sql
} // namespace oceanbase
