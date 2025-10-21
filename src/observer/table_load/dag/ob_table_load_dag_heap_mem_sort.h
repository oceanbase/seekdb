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

#include "observer/table_load/dag/ob_table_load_dag_mem_sort.h"
#include "storage/direct_load/ob_direct_load_external_fragment.h"
#include "storage/direct_load/ob_direct_load_i_table.h"
#include "storage/direct_load/ob_direct_load_table_data_desc.h"
#include "storage/direct_load/ob_direct_load_table_store.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadMemSortOp;

class ObTableLoadHeapMemSorter
{
public:
  ObTableLoadHeapMemSorter();
  int init(ObTableLoadDag *dag, ObTableLoadMemSortOp *op);
  // 数据是按1G一个fragment切分的, 以fragment为单位处理
  int get_next_source_fragment(storage::ObDirectLoadExternalFragment &fragment);
  int add_result_table(const storage::ObDirectLoadTableHandle &table_handle);
  int close();

  const storage::ObDirectLoadTableDataDesc &get_table_data_desc() const
  {
    return table_data_desc_;
  }

public:
  ObTableLoadDag *dag_;
  ObTableLoadMemSortOp *op_;

private:
  storage::ObDirectLoadTableDataDesc table_data_desc_;
  ObDirectLoadExternalFragmentArray source_fragments_;
  int64_t next_source_idx_;
  lib::ObMutex mutex_;
  storage::ObDirectLoadTableHandleArray result_tables_handle_;
  bool is_closed_;
  bool is_inited_;
};

class ObTableLoadHeapMemSortTaskBase : public ObTableLoadDagTaskBase
{
public:
  ObTableLoadHeapMemSortTaskBase(ObTableLoadDag *dag, ObTableLoadHeapMemSorter *mem_sorter);
  ObTableLoadHeapMemSortTaskBase(ObTableLoadHeapMemSortTaskBase *parent);
  virtual ~ObTableLoadHeapMemSortTaskBase() = default;

protected:
  ObTableLoadHeapMemSorter *mem_sorter_;
};

class ObTableLoadHeapMemSortTask final : public share::ObITask,
                                         public ObTableLoadHeapMemSortTaskBase
{
  using ObTableLoadDagTaskBase::dag_;

public:
  ObTableLoadHeapMemSortTask(ObTableLoadDag *dag, ObTableLoadHeapMemSorter *mem_sorter);
  ObTableLoadHeapMemSortTask(ObTableLoadHeapMemSortTaskBase *parent, const int64_t thread_idx);
  virtual ~ObTableLoadHeapMemSortTask() = default;
  int process() override;

private:
  int generate_next_task(ObITask *&next_task) override;

  int do_sort();
  int do_compact(); // 要不要移到compact_table去做?

private:
  class Sorter;
  class Compactor;

private:
  const int64_t thread_idx_;
  int64_t index_dir_id_;
  int64_t data_dir_id_;
  storage::ObDirectLoadTableHandleArray tables_handle_;
};

} // namespace observer
} // namespace oceanbase
