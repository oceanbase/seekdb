/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef _OB_SQL_ENGINE_PDML_PDML_DATA_DRIVER_H_
#define _OB_SQL_ENGINE_PDML_PDML_DATA_DRIVER_H_

#include "sql/engine/pdml/static/ob_pdml_op_batch_row_cache.h"
#include "sql/engine/pdml/static/ob_px_multi_part_modify_op.h"

namespace oceanbase
{
namespace common
{
class ObNewRow;
}

namespace sql
{

class ObExecContext;
struct ObDMLBaseRtDef;
class ObDMLOpTableDesc;
// Operation core class for ObBatchRowCache and storage layer
class ObPDMLOpDataDriver
{
public:
  ObPDMLOpDataDriver(ObEvalCtx *eval_ctx, ObIAllocator &allocator, ObMonitorNode &op_monitor_info):
      returning_ctx_(),
      op_monitor_info_(op_monitor_info),
      cache_(eval_ctx, op_monitor_info),
      reader_(nullptr),
      writer_(nullptr),
      dml_rtdef_(nullptr),
      state_(FILL_CACHE),
      eval_ctx_(eval_ctx),
      last_row_(allocator),
      last_row_tablet_id_(),
      last_row_expr_(nullptr),
      op_id_(common::OB_INVALID_ID),
      is_heap_table_insert_(false),
      with_barrier_(false),
      dfo_id_(OB_INVALID_ID)
  {
  }

  ~ObPDMLOpDataDriver();

  int init(const ObTableModifySpec &spec,
           common::ObIAllocator &allocator,
           ObDMLBaseRtDef &dml_rtdef,
           ObDMLOpDataReader *reader,
           ObDMLOpDataWriter *writer,
           const bool is_heap_table_insert,
           const bool with_barrier = false);

  int destroy();

  int set_dh_barrier_param(uint64_t op_id, const ObPxMultiPartModifyOpInput *modify_input);

  int get_next_row(ObExecContext &ctx, const ObExprPtrIArray &row);
private:
  int fill_cache_unitl_cache_full_or_child_iter_end(ObExecContext &ctx);
  inline int try_write_last_pending_row();
  int switch_to_returning_state(ObExecContext &ctx);
  int switch_row_iter_to_next_partition();
  int barrier(ObExecContext &ctx);
  int next_row_from_cache_for_returning(const ObExprPtrIArray &row);
  int write_partitions(ObExecContext &ctx);
  int set_heap_table_hidden_pk(const ObExprPtrIArray *&row,
                               common::ObTabletID &tablet_id,
                               const bool is_direct_load = false);
  int set_heap_table_hidden_pk_value(const ObExprPtrIArray *&row,
                                     common::ObTabletID &tablet_id,
                                     const uint64_t pk_value);

private:
  // Because cache will cache data from multiple partitions, during iteration it is necessary to
  // Record the current iteration to which partition and which row in the partition it has reached etc. state
  // So, introduce ReturningCtx
  // Used to record which partition's row is currently being returned, to support repeated get_next_row
  struct ReturningCtx {
    ReturningCtx() : next_idx_(0), row_iter_(nullptr) {}
    ~ReturningCtx() = default;
    void reset()
    {
      if (row_iter_) {
        row_iter_->close();
      }
      tablet_id_array_.reset();
      next_idx_ = 0;
      row_iter_ = NULL;
    }

    ObTabletIDArray tablet_id_array_; // All partition indexes to be read
    int64_t next_idx_; // next partition index to read, 0 if not started
    ObPDMLOpRowIterator *row_iter_; // current row iterator of the partition being read
  };

  /* State transition diagram:
   *
   *           start
   *             |
   *             |(0);
   *             |
   *    +---- FILL_CACHE <-----+
   *    |        |             |
   *    |        |(1);          |
   *    |        |             |
   *    |     ROW_RETURNING    |
   *    |     ROW_RETURNING    |
   *    |       ....(2);        |
   *    |     ROW_RETURNING    |
   *    |        |             | (3);
   *    |        |             |
   *    |        |             |
   *    |        +------->-----+
   *    |(4);
   *    +-----> end
   *
   *  (0); start
   *  (1); read data from PDMLDataReader to fill cache
   *  (2); spit out data, as it is a pull data model, this will be done multiple times in state (2);
   *  (3); all cached data has been spit out, start filling the cache again, enter state (1);
   *  (4); no data to fill cache and cache is empty, end
   *
   */
  enum DriverState {
    FILL_CACHE,  /* Populate cache, sync and auto-write to disk when cache is full, and transition to ROW_RETURNING state */
    ROW_RETURNING /* Return the row to the upper layer, automatically switch to filling cache state after all rows are returned */
  };


private:
  ReturningCtx returning_ctx_; // returning type will be used, currently not used
  ObMonitorNode &op_monitor_info_;
  ObPDMLOpBatchRowCache cache_; // used to cache data, needs to be initialized in the init function and allocator allocated
  ObDMLOpDataReader *reader_;
  ObDMLOpDataWriter *writer_;
  ObDMLBaseRtDef *dml_rtdef_;
  DriverState state_; // Driver current state: read/write data state, return data to upper layer state

  ObEvalCtx *eval_ctx_; // used to store last_row as input parameter
  ObChunkDatumStore::LastStoredRow last_row_; // cache the row read from child but not yet written to cache
  common::ObTabletID last_row_tablet_id_; // Cache the part id of the row that has been read from child but not written to cache
  const ObExprPtrIArray *last_row_expr_; // point to expression, used to restore row data to the expression
  int64_t op_id_; // The operator id of this driver for the current operation, used for passing parameters when sending messages in barrier scenarios
  bool is_heap_table_insert_;
  bool with_barrier_; // The current operator needs to support barrier, i.e.: data cannot be output externally before writing is complete
                      // This is for the row-movement scenario to avoid concurrent insert, delete writes to the same row
  uint64_t dfo_id_;   // when with_barrier_ equals true, need to know the DFO corresponding to barrier
  DISALLOW_COPY_AND_ASSIGN(ObPDMLOpDataDriver);;
};
}
}
#endif /* _OB_SQL_ENGINE_PDML_PDML_DATA_DRIVER_H_ */
//// end of header file

