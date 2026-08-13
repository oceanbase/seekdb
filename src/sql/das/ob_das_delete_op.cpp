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

#define USING_LOG_PREFIX SQL_DAS
#include "sql/das/ob_das_delete_op.h"
#include "data_plane/ob_i_dml_service.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/das/ob_das_domain_utils.h"
#include "sql/engine/dml/ob_dml_service.h"
#include "share/schema/ob_schema_struct.h"
namespace oceanbase
{
namespace common
{
namespace serialization
{
template <>
struct EnumEncoder<false, const sql::ObDASDelCtDef *> : sql::DASCtRefEncoder<sql::ObDASDelCtDef>
{
};

template <>
struct EnumEncoder<false, sql::ObDASDelRtDef *> : sql::DASRtRefEncoder<sql::ObDASDelRtDef>
{
};
} // end namespace serialization
} // end namespace common

using namespace common;
using namespace storage;
using namespace share;
namespace sql
{
template <>
int ObDASIndexDMLAdaptor<DAS_OP_TABLE_DELETE, ObDASDMLIterator>::write_rows(const ObTabletID &tablet_id,
                                                                            const CtDefType &ctdef,
                                                                            RtDefType &rtdef,
                                                                            ObDASDMLIterator &iter,
                                                                            int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  data_plane::ObIDmlService *as = ::oceanbase::share::server_service<::oceanbase::data_plane::ObIDmlService>();
  if (OB_UNLIKELY(ctdef.table_param_.get_data_table().is_vector_delta_buffer() &&
                  !ctdef.is_access_main_table_)) {
    // for vector delta buffer, only do insert when DML with main table
    if (OB_FAIL(as->insert_rows(tablet_id, *tx_desc_, dml_execution_,
                                ctdef.column_ids_, &iter, affected_rows))) {
      if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
        LOG_WARN("insert rows to access service failed", K(ret), K(tablet_id));
      }
    }
  } else if (ctdef.table_param_.get_data_table().is_hybrid_vector_index() &&
             !ctdef.is_access_main_table_) {
    // For hybrid vector index, check if it's embedded table
    if (share::schema::is_hybrid_vec_index_embedded_type(ctdef.table_param_.get_data_table().get_index_type())) {
      // For embedded table, perform actual delete operation
      if (OB_FAIL(as->delete_rows(tablet_id, *tx_desc_, dml_execution_, ctdef.column_ids_, &iter, affected_rows))) {
        if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
          LOG_WARN("delete rows to access service failed", K(ret), K(tablet_id));
        }
      }
    } else if (share::schema::is_hybrid_vec_index_log_type(ctdef.table_param_.get_data_table().get_index_type())) {
      // For other hybrid vector index tables (like log table), perform insert to record delete mark
      if (OB_FAIL(as->insert_rows(tablet_id, *tx_desc_, dml_execution_, ctdef.column_ids_, &iter, affected_rows))) {
        if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
          LOG_WARN("insert rows to access service failed", K(ret), K(tablet_id));
        }
      }
    }
  } else if (OB_FAIL(as->delete_rows(tablet_id,
                              *tx_desc_,
                              dml_execution_,
                              ctdef.column_ids_,
                              &iter,
                              affected_rows))) {
    if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
      LOG_WARN("delete rows to access service failed", K(ret));
    }
  } else if (!(ctdef.is_ignore_ || 
            ctdef.table_param_.get_data_table().is_domain_index())
      && 0 == affected_rows) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected affected_rows after do delete", K(affected_rows), K(ret));
  }
  return ret;
}

ObDASDeleteOp::ObDASDeleteOp(ObIAllocator &op_alloc)
  : ObIDASTaskOp(op_alloc),
    del_ctdef_(nullptr),
    del_rtdef_(nullptr),
    write_buffer_(),
    affected_rows_(0)
{
}

int ObDASDeleteOp::open_op()
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  common::ObSEArray<ObFTDocWordInfo, 4> doc_word_infos;
  doc_word_infos.set_attr(lib::ObMemAttr("FTDocWInfo"));
  ObDASDMLIterator dml_iter(
      del_ctdef_, write_buffer_, op_alloc_, srs_provider_,
      lob_read_options_);
  ObDASIndexDMLAdaptor<DAS_OP_TABLE_DELETE, ObDASDMLIterator> del_adaptor;
  del_adaptor.tx_desc_ = trans_desc_;
  del_adaptor.snapshot_ = snapshot_;
  del_adaptor.write_branch_id_ = write_branch_id_;
  del_adaptor.ctdef_ = del_ctdef_;
  del_adaptor.rtdef_ = del_rtdef_;
  del_adaptor.related_ctdefs_ = &related_ctdefs_;
  del_adaptor.related_rtdefs_ = &related_rtdefs_;
  del_adaptor.tablet_id_ = tablet_id_;
  del_adaptor.related_tablet_ids_ = &related_tablet_ids_;
  del_adaptor.use_snapshot_opt_ = das_snapshot_opt_info_.use_specify_snapshot_;
  del_adaptor.das_allocator_ = &op_alloc_;
  del_adaptor.ft_doc_word_infos_ = &doc_word_infos;
  if (OB_FAIL(ObDASDomainUtils::build_ft_doc_word_infos(trans_desc_, snapshot_, related_ctdefs_, related_tablet_ids_,
          del_ctdef_->is_main_table_in_fts_ddl_, doc_word_infos))) {
  } else if (OB_FAIL(del_adaptor.write_tablet(dml_iter, affected_rows))) {
    if (OB_TRY_LOCK_ROW_CONFLICT != ret) {
      LOG_WARN("delete row to partition storage failed", K(ret));
    }
  } else {
    affected_rows_ = affected_rows;
  }
  return ret;
}

int ObDASDeleteOp::record_task_result_to_rtdef()
{
  int ret = OB_SUCCESS;
  del_rtdef_->affected_rows_ += affected_rows_;
  return ret;
}

int ObDASDeleteOp::assign_task_result(ObIDASTaskOp *other)
{
  int ret = OB_SUCCESS;
  if (other->get_type() != get_type()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected task type", K(ret), KPC(other));
  } else {
    ObDASDeleteOp *del_op = static_cast<ObDASDeleteOp *>(other);
    affected_rows_ = del_op->get_affected_rows();
  }
  return ret;
}

int ObDASDeleteOp::release_op()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObDASDeleteOp::init_task_info(uint32_t row_extend_size)
{
  int ret = OB_SUCCESS;
  if (!write_buffer_.is_inited()
      && OB_FAIL(write_buffer_.init(op_alloc_, row_extend_size, "DASDeleteBuffer"))) {
    LOG_WARN("init delete buffer failed", K(ret));
  }
  return ret;
}

int ObDASDeleteOp::write_row(const ExprFixedArray &row,
                             ObEvalCtx &eval_ctx,
                             ObChunkDatumStore::StoredRow *&stored_row)
{
  int ret = OB_SUCCESS;
  if (!write_buffer_.is_inited()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("buffer not inited", K(ret));
  } else if (OB_FAIL(write_buffer_.add_row(row, &eval_ctx, stored_row, true))) {
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObDASDeleteOp, ObIDASTaskOp),
                    del_ctdef_,
                    del_rtdef_,
                    write_buffer_);

}  // namespace sql
}  // namespace oceanbase
