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

#ifndef _OB_SQL_ENGINE_PDML_PX_MULTI_PART_DELETE_OP_H_
#define _OB_SQL_ENGINE_PDML_PX_MULTI_PART_DELETE_OP_H_

#include "lib/container/ob_fixed_array.h"
#include "sql/engine/dml/ob_table_modify_op.h"
#include "sql/engine/pdml/static/ob_pdml_op_data_driver.h"

namespace oceanbase
{
namespace storage
{
class ObDMLBaseParam;
}
namespace sql
{
class ObPxMultiPartDeleteOpInput : public ObPxMultiPartModifyOpInput
{
  OB_UNIS_VERSION_V(1);
public:
  ObPxMultiPartDeleteOpInput(ObExecContext &ctx, const ObOpSpec &spec)
    : ObPxMultiPartModifyOpInput(ctx, spec)
  {}
  int init(ObTaskInfo &task_info) override
  {
    return ObPxMultiPartModifyOpInput::init(task_info);
  }
private:
  DISALLOW_COPY_AND_ASSIGN(ObPxMultiPartDeleteOpInput);
};

class ObPxMultiPartDeleteSpec : public ObTableModifySpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObPxMultiPartDeleteSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObTableModifySpec(alloc, type),
    row_desc_(),
    del_ctdef_(alloc),
    with_barrier_(false)
  {
  }
  //This interface is only allowed to be used in a single-table DML operator,
  //it is invalid when multiple tables are modified in one DML operator
  virtual int get_single_dml_ctdef(const ObDMLBaseCtDef *&dml_ctdef) const override
  {
    dml_ctdef = &del_ctdef_;
    return common::OB_SUCCESS;
  }

  void set_with_barrier(bool w) { with_barrier_ = w; }
  virtual bool is_pdml_operator() const override { return true; } // pdml delete is both dml and pdml
  int register_to_datahub(ObExecContext &ctx) const override;
public:
  ObDMLOpRowDesc row_desc_;  // record the position of partition id column in the row's cell
  ObDelCtDef del_ctdef_;
  // Due to the existence of update row movement, the update plan will expand to:
  //  INSERT
  //    DELETE
  //
  // INSERT needs to wait for the DELETE operator to complete execution before it can output data
  bool with_barrier_;
  DISALLOW_COPY_AND_ASSIGN(ObPxMultiPartDeleteSpec);
};

class ObPxMultiPartDeleteOp : public ObDMLOpDataReader,
                            public ObDMLOpDataWriter,
                            public ObTableModifyOp
{
  OB_UNIS_VERSION(1);
public:
  ObPxMultiPartDeleteOp(ObExecContext &exec_ctx,
                        const ObOpSpec &spec,
                        ObOpInput *input)
  : ObTableModifyOp(exec_ctx, spec, input),
    data_driver_(&ObOperator::get_eval_ctx(), exec_ctx.get_allocator(), op_monitor_info_)
  {
  }

public:
  virtual bool has_foreign_key() const  { return false; } // Default implementation, do not consider foreign key issues for now
  // impl. ObDMLDataReader
  // Read a line of data from child op and cache it to the ObPxMultiPartDelete operator
  // Also responsible for calculating the corresponding partition_id for this row
  int read_row(ObExecContext &ctx,
               const ObExprPtrIArray *&row,
               common::ObTabletID &tablet_id,
               bool &is_skipped) override;
  // impl. ObDMLDataWriter
  // Write cached data in bulk to the storage layer
  int write_rows(ObExecContext &ctx,
                 const ObDASTabletLoc *tablet_loc,
                 ObPDMLOpRowIterator &iterator) override;
  // Upper layer op reads a line from ObPxMultiPartDelete
  virtual int inner_get_next_row();
  virtual int inner_open();
  virtual int inner_close();
  virtual void destroy()
  {
    // destroy
    del_rtdef_.~ObDelRtDef();
    ObTableModifyOp::destroy();
  }
private:
  ObPDMLOpDataDriver data_driver_;
  ObDelRtDef del_rtdef_;
  DISALLOW_COPY_AND_ASSIGN(ObPxMultiPartDeleteOp);
};
}

}
#endif /* _OB_SQL_ENGINE_PDML_PX_MULTI_PART_DELETE_OP_H_ */
//// end of header file

