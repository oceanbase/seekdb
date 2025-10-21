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

#include "observer/table_load/ob_table_load_merge_table_op.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadMergeDataOp final : public ObTableLoadMergeTableBaseOp
{
public:
  ObTableLoadMergeDataOp(ObTableLoadMergeTableBaseOp *parent);
  virtual ~ObTableLoadMergeDataOp();

protected:
  int switch_next_op(bool is_parent_called) override;
  int acquire_child_op(ObTableLoadMergeOpType::Type child_op_type, ObIAllocator &allocator,
                       ObTableLoadMergeOp *&child) override;

private:
  enum Status
  {
    NONE = 0,
    MEM_SORT,
    COMPACT_TABLE,
    INSERT_SSTABLE,
    COMPLETED
  };
  Status status_;
};

} // namespace observer
} // namespace oceanbase
