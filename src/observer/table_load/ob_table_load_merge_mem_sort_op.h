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
class ObTableLoadMemCompactor;
class ObTableLoadMultipleHeapTableCompactor;

class ObTableLoadMergeMemSortOp final : public ObTableLoadMergeTableBaseOp
{
  friend class ObTableLoadMemCompactor;
  friend class ObTableLoadMultipleHeapTableCompactor;

public:
  ObTableLoadMergeMemSortOp(ObTableLoadMergeTableBaseOp *parent);
  virtual ~ObTableLoadMergeMemSortOp();
  int on_success() override;
  void stop() override;

protected:
  int switch_next_op(bool is_parent_called) override;

private:
  ObTableLoadMemCompactor *mem_compactor_;
  ObTableLoadMultipleHeapTableCompactor *multiple_heap_table_compactor_;
};

} // namespace observer
} // namespace oceanbase
