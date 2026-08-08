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

#ifndef OCEANBASE_DATA_PLANE_API_OB_I_DML_SERVICE_H_
#define OCEANBASE_DATA_PLANE_API_OB_I_DML_SERVICE_H_

#include "data_plane/memtable/ob_write_flag.h"

#include <stdint.h>
#include "data_plane/access/ob_dml_execution.h"

namespace oceanbase
{
namespace blocksstable
{
class ObDatumRowIterator;
}
namespace common
{
class ObIAllocator;
class ObTabletID;
template <typename T> class ObIArray;
}
namespace transaction
{
class ObTxDesc;
class ObTxReadSnapshot;
}
namespace data_plane
{

class ObDmlTablePlan;
class ObWriteContext;

enum class ObDuplicateReturnMode : int
{
  ALL = 0,
  ONE = 1,
};

enum class ObRowLockMode : int
{
  NONE = 0,
  WRITE = 1,
};

// Isolation seam for row writes.  Callers prepare one opaque execution from
// query-owned values, then reuse it across the row operation without seeing
// storage parameters or schema implementations.
class ObIDmlService
{
public:
  virtual ~ObIDmlService() {}

  virtual int prepare_execution(
      const ObDmlWriteSpec &write_spec,
      const ObDmlTablePlan &table_plan,
      const transaction::ObTxReadSnapshot &snapshot,
      common::ObIAllocator &allocator,
      const ObWriteContext &write_context,
      const concurrent_control::ObWriteFlag &write_flag,
      ObDmlExecution &execution) = 0;

  virtual int delete_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) = 0;

  virtual int put_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) = 0;

  virtual int insert_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) = 0;

  virtual int insert_rows_fetch_duplicates(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      const common::ObIArray<uint64_t> &duplicated_column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      const ObDuplicateReturnMode return_mode,
      int64_t &affected_rows,
      blocksstable::ObDatumRowIterator *&duplicated_rows) = 0;

  virtual void free_duplicate_rows_iterator(
      blocksstable::ObDatumRowIterator *iterator) = 0;

  virtual int update_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const common::ObIArray<uint64_t> &column_ids,
      const common::ObIArray<uint64_t> &updated_column_ids,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) = 0;

  virtual int lock_rows(
      const common::ObTabletID &tablet_id,
      transaction::ObTxDesc &tx_desc,
      const ObDmlExecution &execution,
      const int64_t abs_lock_timeout,
      const ObRowLockMode lock_mode,
      blocksstable::ObDatumRowIterator *row_iter,
      int64_t &affected_rows) = 0;

protected:
  static void bind_execution(
      ObDmlExecution &execution,
      ObIDmlExecutionState *state)
  {
    execution.bind(state);
  }

  static ObIDmlExecutionState *execution_state(const ObDmlExecution &execution)
  {
    return execution.state();
  }
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_I_DML_SERVICE_H_
