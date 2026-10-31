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

#pragma once

#include "common/datum/datum.h"
#include "common/datum/sql_type.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace blocksstable
{
struct ObStorageDatum;
}
namespace sql
{
class ObExpr;

// An execution-owned, fixed-address output for one complete local point get.
// The storage projector borrows it; only the final TableScan success publishes
// a readable view. Legacy Expr projection remains independent and unchanged.
class NativeBigintOutput final
{
public:
  static constexpr int64_t MAX_COLUMNS = 3;

  NativeBigintOutput();
  NativeBigintOutput(const NativeBigintOutput &) = delete;
  NativeBigintOutput &operator=(const NativeBigintOutput &) = delete;
  NativeBigintOutput(NativeBigintOutput &&) = delete;
  NativeBigintOutput &operator=(NativeBigintOutput &&) = delete;

  int prepare(const common::ObIArray<ObExpr *> &outputs, bool &supported);
  // Bind by Expr identity through the actual storage projector. Output order
  // and duplicate expressions need not match the storage row's column order.
  void bind_projector(const common::ObIArray<ObExpr *> &access_exprs,
                      const common::ObIArray<int32_t> &projector);
  void project(const blocksstable::ObStorageDatum *datums, int64_t batch_idx);

  // Always advance the token, including failed/end/unsupported operations, so
  // a preceding ResultRowView cannot remain readable merely because slots live.
  void invalidate();
  void publish(int result, int64_t row_count, bool end, bool skipped);
  bool try_get(int64_t batch_idx, const common::Datum *&values,
               const common::SqlType *&type, int64_t &count,
               uint64_t &generation, const uint64_t *&current_generation) const;

private:
  common::Datum values_[MAX_COLUMNS];
  const ObExpr *output_exprs_[MAX_COLUMNS];
  int32_t storage_indexes_[MAX_COLUMNS];
  const common::SqlType *type_;
  int64_t count_;
  uint64_t generation_;
  bool layout_bound_;
  bool candidate_;
  bool ready_;
};

} // namespace sql
} // namespace oceanbase
