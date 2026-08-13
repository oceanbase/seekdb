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

#ifndef DEV_SRC_SQL_DAS_OB_DAS_INSERT_OP_H_
#define DEV_SRC_SQL_DAS_OB_DAS_INSERT_OP_H_
#include "sql/das/ob_das_task.h"
#include "data_plane/ob_i_dml_service.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "sql/das/ob_das_dml_ctx_define.h"
namespace oceanbase
{
namespace sql
{
typedef common::ObList<blocksstable::ObDatumRowIterator *, common::ObIAllocator> ObDuplicatedIterList;
class ObDASConflictIterator : public blocksstable::ObDatumRowIterator
{
public:
  ObDASConflictIterator(const ObjMetaFixedArray &output_types,
                        common::ObIAllocator &alloc)
    : output_types_(output_types),
      duplicated_iter_list_(alloc),
      curr_iter_(duplicated_iter_list_.begin())
  {
  }

  ~ObDASConflictIterator() {};

  void reset();
  virtual int get_next_row(blocksstable::ObDatumRow *&row) override;

  void init_curr_iter()
  { curr_iter_ = duplicated_iter_list_.begin(); }
  ObDuplicatedIterList &get_duplicated_iter_array()
  { return duplicated_iter_list_; }
private:
  const ObjMetaFixedArray &output_types_;
  ObDuplicatedIterList duplicated_iter_list_;
  ObDuplicatedIterList::iterator curr_iter_;
};

class ObDASInsertOp : public ObIDASTaskOp
{
  OB_UNIS_VERSION(1);
public:
  ObDASInsertOp(common::ObIAllocator &op_alloc);
  virtual ~ObDASInsertOp() = default;

  virtual int open_op() override;
  virtual int release_op() override;
  virtual int record_task_result_to_rtdef() override;
  virtual int assign_task_result(ObIDASTaskOp *other) override;
  virtual int init_task_info(uint32_t row_extend_size) override;
  virtual const ObDASBaseCtDef *get_ctdef() const override { return ins_ctdef_; }
  virtual ObDASBaseRtDef *get_rtdef() override { return ins_rtdef_; }
  int write_row(const ExprFixedArray &row,
                ObEvalCtx &eval_ctx,
                ObChunkDatumStore::StoredRow *&stored_row);
  int64_t get_row_cnt() const { return insert_buffer_.get_row_cnt(); }
  ObDASWriteBuffer &get_insert_buffer() { return insert_buffer_; }
  void set_das_ctdef(const ObDASInsCtDef *ins_ctdef) { ins_ctdef_ = ins_ctdef; }
  void set_das_rtdef(ObDASInsRtDef *ins_rtdef) { ins_rtdef_ = ins_rtdef; }
  virtual int dump_data() const override
  {
    return insert_buffer_.dump_data(*ins_ctdef_);
  }

  blocksstable::ObDatumRowIterator *get_duplicated_result()
  { return result_; }

  int64_t get_affected_rows() { return affected_rows_; }
  bool get_is_duplicated() { return is_duplicated_; }

  INHERIT_TO_STRING_KV("parent", ObIDASTaskOp,
                       KPC_(ins_ctdef),
                       KPC_(ins_rtdef),
                       K_(insert_buffer));

private:
  int insert_rows();
  int insert_row_with_fetch();

  int insert_index_with_fetch(data_plane::ObDmlExecution &execution,
                              data_plane::ObIDmlService *dml_service,
                              blocksstable::ObDatumRowIterator &dml_iter,
                              ObDASConflictIterator *result_iter,
                              const ObDASInsCtDef *ins_ctdef,
                              ObDASInsRtDef *ins_rtdef,
                              data_plane::ObWriteContext &write_context,
                              const UIntFixedArray *duplicated_column_ids,
                              common::ObTabletID tablet_id,
                              transaction::ObTxReadSnapshot *snapshot);

private:
  const ObDASInsCtDef *ins_ctdef_;
  ObDASInsRtDef *ins_rtdef_;
  ObDASWriteBuffer insert_buffer_;
  blocksstable::ObDatumRowIterator *result_;
  int64_t affected_rows_;  // local execute result, no need to serialize
  bool is_duplicated_;  // local execute result, no need to serialize
};
}  // namespace sql
}  // namespace oceanbase
#endif /* DEV_SRC_SQL_DAS_OB_DAS_INSERT_OP_H_ */
