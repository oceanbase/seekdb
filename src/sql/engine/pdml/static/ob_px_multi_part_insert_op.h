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

#ifndef OB_PX_MULTI_PART_INSERT_OP_H_
#define OB_PX_MULTI_PART_INSERT_OP_H_

#include "lib/container/ob_fixed_array.h"
#include "sql/engine/dml/ob_table_modify_op.h"
#include "sql/engine/pdml/static/ob_pdml_op_data_driver.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadTableCtx;
}
namespace sql
{
class ObPxMultiPartInsertOpInput : public ObPxMultiPartModifyOpInput
{
  OB_UNIS_VERSION_V(1);
public:
  ObPxMultiPartInsertOpInput(ObExecContext &ctx, const ObOpSpec &spec)
    : ObPxMultiPartModifyOpInput(ctx, spec),
      error_code_(OB_SUCCESS)
  {}
  int init(ObTaskInfo &task_info) override
  {
    return ObPxMultiPartModifyOpInput::init(task_info);
  }
  void reset() override
  {
    ObPxMultiPartModifyOpInput::reset();
    error_code_ = OB_SUCCESS;
  }
  inline void set_error_code(const int error_code) { error_code_ = error_code; }
  inline int get_error_code() const { return error_code_; }
  TO_STRING_KV(K_(error_code));
private:
  int error_code_;
  DISALLOW_COPY_AND_ASSIGN(ObPxMultiPartInsertOpInput);
};

class ObPxMultiPartInsertSpec : public ObTableModifySpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObPxMultiPartInsertSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObTableModifySpec(alloc, type),
    row_desc_(),
    ins_ctdef_(alloc)
  {
  }
  //This interface is only allowed to be used in a single-table DML operator,
  //it is invalid when multiple tables are modified in one DML operator
  virtual int get_single_dml_ctdef(const ObDMLBaseCtDef *&dml_ctdef) const override
  {
    dml_ctdef = &ins_ctdef_;
    return common::OB_SUCCESS;
  }
  virtual bool is_pdml_operator() const override { return true; }
public:
  ObDMLOpRowDesc row_desc_;  // record the position of partition id column in the row's cell
  ObInsCtDef ins_ctdef_;
  DISALLOW_COPY_AND_ASSIGN(ObPxMultiPartInsertSpec);
};

class ObPxMultiPartInsertOp : public ObDMLOpDataReader,
                            public ObDMLOpDataWriter,
                            public ObTableModifyOp
{
  OB_UNIS_VERSION(1);
public:
  ObPxMultiPartInsertOp(ObExecContext &exec_ctx,
                        const ObOpSpec &spec,
                        ObOpInput *input)
  : ObTableModifyOp(exec_ctx, spec, input),
    data_driver_(&ObOperator::get_eval_ctx(), exec_ctx.get_allocator(), op_monitor_info_),
    ins_rtdef_(),
    table_ctx_(nullptr)
  {
  }

public:
  virtual bool has_foreign_key() const  { return false; } // Default implementation, foreign key issues are not considered for now

  int read_row(ObExecContext &ctx,
               const ObExprPtrIArray *&row,
               common::ObTabletID &tablet_id,
               bool &is_skipped) override;
  int write_rows(ObExecContext &ctx,
                 const ObDASTabletLoc *tablet_loc,
                 ObPDMLOpRowIterator &iterator) override;

  virtual int inner_get_next_row();
  virtual int inner_open();
  virtual int inner_close();
  virtual void destroy()
  {
    // destroy
    ins_rtdef_.~ObInsRtDef();
    ObTableModifyOp::destroy();
  }
protected:
  ObPDMLOpDataDriver data_driver_;
  ObInsRtDef ins_rtdef_;
  observer::ObTableLoadTableCtx *table_ctx_; // deprecated
  DISALLOW_COPY_AND_ASSIGN(ObPxMultiPartInsertOp);
};


}  // namespace sql
}  // namespace oceanbase
#endif /* OB_PX_MULTI_PART_INSERT_OP_H_ */
