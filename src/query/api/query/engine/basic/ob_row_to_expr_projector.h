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

#ifndef OCEANBASE_QUERY_API_ENGINE_BASIC_OB_ROW_TO_EXPR_PROJECTOR_H_
#define OCEANBASE_QUERY_API_ENGINE_BASIC_OB_ROW_TO_EXPR_PROJECTOR_H_

#include "common/datum/datum.h"
#include "lib/container/ob_se_array.h"
#include "query/engine/basic/ob_pushdown_filter.h"

namespace oceanbase
{
namespace blocksstable
{
struct ObStorageDatum;
}
namespace storage
{

// Query-owned projection protocol invoked by the data plane after a scan.
// The storage namespace is retained temporarily for source compatibility;
// ownership is defined by this query API header.
class ObRow2ExprsProjector
{
public:
  explicit ObRow2ExprsProjector(common::ObIAllocator &allocator)
    : has_virtual_(false),
      op_(nullptr),
      outputs_(common::OB_MALLOC_NORMAL_BLOCK_SIZE, common::ModulePageAllocator(allocator))
  {}
  ~ObRow2ExprsProjector() { destroy(); }

  int init(const sql::ObExprPtrIArray &exprs,
           sql::ObPushdownOperator &op,
           const common::ObIArray<int32_t> &projector);
  int project(const sql::ObExprPtrIArray &exprs,
              const blocksstable::ObStorageDatum *datums,
              int16_t *nop_pos,
              int64_t &nop_cnt);
  void destroy() { outputs_.reset(); }
  bool has_virtual() const { return has_virtual_; }

private:
  struct Item
  {
    int32_t obj_idx_;
    int32_t expr_idx_;
    sql::ObExprFrameWriteTarget target_;
    sql::ObEvalInfo *eval_info_;
    sql::ObBitVector *eval_flags_;


    Item() = default;
    DECLARE_TO_STRING;
  };
  static_assert(sizeof(Item) == 56, "Projection cache includes its 32-byte selected destination");

  bool has_virtual_;
  sql::ObPushdownOperator *op_;
  common::ObSEArray<Item, 4> outputs_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_BASIC_OB_ROW_TO_EXPR_PROJECTOR_H_
