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

#include "common/datum/datum_parameter_array.h"
#include "lib/container/ob_array.h"

namespace oceanbase
{
namespace sql
{

// Each appended row owns its Datum payloads in allocator_. Copying or decoding
// a batch therefore does not borrow the source batch or receive buffer.
struct ObBatchRescanParams {
  OB_UNIS_VERSION_V(1);
public:
  using Row = common::ObSEArray<common::DatumParameter, 0>;
  // Captures each value before another expression can overwrite its buffer.
  // The owner must not be reset while its row builder is alive.
  class RowBuilder
  {
  public:
    explicit RowBuilder(ObBatchRescanParams &owner)
        : owner_(owner), values_(4 * sizeof(common::DatumParameter)), finished_(false) {}
    int append(const common::DatumParameter &value);
    int finish(const common::ObIArray<int64_t> &param_idxs,
               const common::ObIArray<int64_t> &param_expr_idxs);
  private:
    ObBatchRescanParams &owner_;
    Row values_;
    bool finished_;
    DISALLOW_COPY_AND_ASSIGN(RowBuilder);
  };

  ObBatchRescanParams() :allocator_("PxBatchRescan"), params_(),
      param_idxs_(), param_expr_idxs_(0) {}
  ~ObBatchRescanParams() = default;
  int64_t get_count() const { return params_.count(); }
  common::ObIArray<common::DatumParameter>& get_one_batch_params(int64_t idx) {
    return params_.at(idx);
  }
  int64_t get_param_idx(int64_t idx) {
    return param_idxs_.at(idx);
  }
  int append_batch_rescan_param(const common::ObIArray<int64_t> &param_idxs,
      const common::ObIArray<common::DatumParameter> &values);
  int append_batch_rescan_param(const common::ObIArray<int64_t> &param_idxs,
      const common::ObIArray<common::DatumParameter> &values,
      const common::ObIArray<int64_t> &param_expr_idxs);
  void reset()
  {
    params_.reset();
    param_idxs_.reset();
    param_expr_idxs_.reset();
    allocator_.reset();
  }

  void reuse()
  {
    params_.reset();
    param_idxs_.reset();
    param_expr_idxs_.reset();
    allocator_.reuse();
  }

  int assign(const ObBatchRescanParams &other);
public:
  common::ObArenaAllocator allocator_;
  common::ObSArray<Row> params_;
  common::ObSEArray<int64_t, 8> param_idxs_;
  common::ObSEArray<int64_t, 8> param_expr_idxs_;
  TO_STRING_KV(K(param_idxs_), K(params_), K_(param_expr_idxs));
private:
  int validate_shape() const;
  int validate_append(const common::ObIArray<int64_t> &param_idxs, int64_t count,
                      const common::ObIArray<int64_t> &param_expr_idxs) const;
  int append_captured_row(const common::ObIArray<int64_t> &param_idxs, const Row &values,
                          const common::ObIArray<int64_t> &param_expr_idxs);
};

} // namespace sql
} // namespace oceanbase
