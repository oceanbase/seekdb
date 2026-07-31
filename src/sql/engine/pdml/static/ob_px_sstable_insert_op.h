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

#ifndef OCEANBASE_SQL_PX_OB_PX_SSTABLE_INSERT_OP_H
#define OCEANBASE_SQL_PX_OB_PX_SSTABLE_INSERT_OP_H

#include "sql/engine/pdml/static/ob_px_multi_part_insert_op.h"
#include "share/ob_tablet_autoincrement_param.h"

namespace oceanbase
{
namespace storage
{
struct ObInsertMonitor;
struct ObTabletSliceParam;
class ObDDLInsertDag;
class ObISliceWriter;
struct ObDDLAutoincParam;
}

namespace sql
{
class ObPxMultiPartSSTableInsertOpInput : public ObPxMultiPartModifyOpInput
{
  OB_UNIS_VERSION_V(1);
public:
  ObPxMultiPartSSTableInsertOpInput(ObExecContext &ctx, const ObOpSpec &spec)
    : ObPxMultiPartModifyOpInput(ctx, spec)
  {}
private:
  DISALLOW_COPY_AND_ASSIGN(ObPxMultiPartSSTableInsertOpInput);
};

class ObPxMultiPartSSTableInsertSpec : public ObPxMultiPartInsertSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObPxMultiPartSSTableInsertSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObPxMultiPartInsertSpec(alloc, type), snapshot_query_expr_(nullptr),
      regenerate_heap_table_pk_(false)
  {}
  int get_snapshot_version(ObEvalCtx &eval_ctx, int64_t &snapshot_version) const;
public:
  ObExpr *snapshot_query_expr_;
  bool regenerate_heap_table_pk_;
  int64_t ddl_slice_id_idx_; // record idx of exprs for ddl slice id
  DISALLOW_COPY_AND_ASSIGN(ObPxMultiPartSSTableInsertSpec);
};

class ObPxMultiPartSSTableInsertOp : public ObPxMultiPartInsertOp
{
public:
  ObPxMultiPartSSTableInsertOp(ObExecContext &exec_ctx,
                               const ObOpSpec &spec,
                               ObOpInput *input)
    : ObPxMultiPartInsertOp(exec_ctx, spec, input),
      allocator_("SSTABLE_INS"),
      is_all_partition_finished_(false),
      is_partitioned_table_(false),
      is_vec_gen_vid_(false),
      tablet_id_expr_(nullptr),
      slice_info_expr_(nullptr),
      tablet_autoinc_expr_(nullptr),
      tablet_autoinc_column_idx_(-1),
      ddl_dag_(nullptr),
      need_idempotent_tablet_autoinc_(false),
      need_idempotent_table_autoinc_(false),
      need_idempotent_doc_id_(false)
  {}
  virtual ~ObPxMultiPartSSTableInsertOp() { destroy(); }
  const ObPxMultiPartSSTableInsertSpec &get_spec() const;
  virtual int inner_open() override;
  virtual int inner_get_next_row() override;
  virtual void destroy() override;
protected:
  int get_next_row_from_child(ObInsertMonitor *insert_monitor);
  int get_tablet_info_from_row(
      const ObExprPtrIArray &row,
      common::ObTabletID &tablet_id,
      storage::ObTabletSliceParam *tablet_slice_param = nullptr);
  int eval_current_row(const int64_t rowkey_column_count, blocksstable::ObDatumRow &current_row);
  int eval_current_row(ObIArray<ObDatum *> &datums);
  int sync_table_level_autoinc_value();
  bool is_heap_plan() const { return MY_SPEC.regenerate_heap_table_pk_ || is_vec_gen_vid_; }
  int write_heap_slice_by_row();
  int write_ordered_slice_by_row();
  int finish_dag();
  bool need_autoinc_by_row();
  int get_data_tablet_id(const ObTabletID &tablet_id, ObTabletID &data_tablet_id);
  int sync_tablet_doc_id(ObISliceWriter *slice_writer);
  int init_table_autoinc_param(const ObTabletID &tablet_id, const int64_t slice_idx, ObDDLAutoincParam &autoinc_param);
  int init_tablet_autoinc_param(const ObTabletID &tablet_id, const int64_t slice_idx, ObDDLAutoincParam &autoinc_param);
  int locate_exprs();
  int check_need_idempotence();
  int get_or_create_heap_writer(const ObTabletID &tablet_id, ObISliceWriter *&slice_writer);
  int switch_slice_if_need(const ObTabletID &tablet_id, const int64_t slice_idx,
                           ObISliceWriter *&slice_writer, ObDDLAutoincParam *autoinc_param = nullptr);
  
protected:
  static const uint64_t MAP_HASH_BUCKET_NUM = 1543L;
  common::ObArenaAllocator allocator_;
  bool is_all_partition_finished_;
  bool is_partitioned_table_;
  // vector index
  bool is_vec_gen_vid_;

  ObTabletID non_partitioned_tablet_id_;
  ObExpr *tablet_id_expr_; // valid when partitioned table
  ObExpr *slice_info_expr_; // valid when ordered tablet and idempotent ddl
  ObExpr *tablet_autoinc_expr_; // valid when heap plan
  int64_t tablet_autoinc_column_idx_;
  storage::ObDDLInsertDag *ddl_dag_;
  // for heap plan, direct write tablet
  typedef common::hash::ObHashMap<common::ObTabletID, ObISliceWriter *, common::hash::NoPthreadDefendMode> TabletWriterMap;
  TabletWriterMap heap_tablet_writer_map_;
  bool need_idempotent_tablet_autoinc_;
  bool need_idempotent_table_autoinc_;
  bool need_idempotent_doc_id_;
  DISALLOW_COPY_AND_ASSIGN(ObPxMultiPartSSTableInsertOp);
};

}// end namespace sql
}// end namespace oceanbase


#endif//OCEANBASE_SQL_PX_OB_PX_SSTABLE_INSERT_OP_H
