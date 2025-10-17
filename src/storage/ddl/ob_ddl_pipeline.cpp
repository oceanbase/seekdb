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

#define USING_LOG_PREFIX STORAGE
#include "ob_ddl_pipeline.h"
#include "share/vector_index/ob_plugin_vector_index_service.h"
#include "share/vector_index/ob_plugin_vector_index_utils.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "storage/ddl/ob_tablet_slice_writer.h"
#include "storage/lob/ob_lob_util.h"
#include "storage/tx/ob_trans_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/expr/ob_array_expr_utils.h"

using namespace oceanbase::storage;
using namespace oceanbase::common;
using namespace oceanbase::share;

int ObIDDLPipeline::init(
    const ObTabletID &tablet_id,
    const int64_t slice_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() || slice_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(slice_idx));
  } else {
    tablet_id_ = tablet_id;
    slice_idx_ = slice_idx;
  }
  return ret;
}

int ObIDDLPipeline::process()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(preprocess())) {
    LOG_WARN("preprocess failed", K(ret));
  } else {
    static const int64_t timeout_us = 1000L; // 1ms
    ObChunk *chunk = nullptr;
    while (OB_SUCC(ret)) {
      if (OB_UNLIKELY(dag_->is_final_status())) {
        ret = dag_->get_dag_ret();
        FLOG_INFO("dag is stoped", K(ret));
        break;
      } else if (OB_FAIL(get_next_chunk(chunk))) {
        if (OB_ENTRY_NOT_EXIST == ret) {
          ret = OB_DAG_TASK_IS_SUSPENDED;
          break;
        }
      } else if (OB_ISNULL(chunk)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("chunk is null", K(ret), KP(chunk));
      } else {
        int tmp_ret = OB_SUCCESS;
        if (OB_FAIL(push(*chunk))) {
          LOG_WARN("excute chunk failed", K(ret), KPC(chunk));
        } else if (chunk->is_end_chunk()) {
          ret = OB_ITER_END;
        }
        // ignore ret, always finish chunk
        if (OB_TMP_FAIL(finish_chunk(chunk))) {
          LOG_WARN("finish chunk failed", K(tmp_ret), KPC(chunk));
        }
      }
    }
  }
  postprocess(ret);
  return ret;
}

ObVectorIndexTabletContext::ObVectorIndexTabletContext()
    : row_cnt_(0), vec_dim_(0), tenant_id_(MTL_ID()), ls_id_(), tablet_id_(), vec_idx_param_(), ctx_(),
      vector_vid_col_idx_(0), vector_col_idx_(0), vector_key_col_idx_(0), vector_data_col_idx_(0), center_id_col_idx_(0), center_vector_col_idx_(0),
      meta_id_col_idx_(0), meta_vector_col_idx_(0), pq_center_id_col_idx_(0), pq_center_vector_col_idx_(0), extra_column_idx_types_(),
      lob_inrow_threshold_(0), rowkey_cnt_(0), column_cnt_(0), snapshot_version_(0), index_type_(share::VIAT_MAX), helper_(nullptr),
      allocator_("VecIndexCtx", OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID()),
      memory_context_(MTL(ObPluginVectorIndexService *)->get_memory_context()),
      all_vsag_use_mem_(MTL(ObPluginVectorIndexService *)->get_all_vsag_use_mem())
{

}

int ObVectorIndexTabletContext::init(
    const ObLSID &ls_id,
    const ObTabletID &tablet_id,
    const ObIndexType &index_type,
    const int64_t snapshot_version,
    const ObDDLTableSchema &ddl_table_schema)
{
  int ret = OB_SUCCESS;
  if (!ls_id.is_valid() || !tablet_id.is_valid() || snapshot_version <= 0 || !(ddl_table_schema.table_item_.vec_dim_ > 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(ls_id), K(tablet_id), K(ddl_table_schema), K(snapshot_version));
  } else {
    row_cnt_ = 0;
    vec_dim_ = ddl_table_schema.table_item_.vec_dim_;
    ls_id_ = ls_id;
    tablet_id_ = tablet_id;
    vec_idx_param_ = ddl_table_schema.table_item_.vec_idx_param_;
    ctx_.ls_id_ = ls_id_;
    lob_inrow_threshold_ = ddl_table_schema.table_item_.lob_inrow_threshold_;
    rowkey_cnt_ = ddl_table_schema.table_item_.rowkey_column_num_;
    column_cnt_ = ddl_table_schema.column_items_.count();
    snapshot_version_ = snapshot_version;
    if (schema::is_vec_index_snapshot_data_type(index_type)) {
      if (OB_FAIL(init_hnsw_index(ddl_table_schema))) {
        LOG_WARN("init hnsw index failed", K(ret));
      }
    } else if (schema::is_local_vec_ivf_centroid_index(index_type)) {
      if (OB_FAIL(init_ivf_center_index(ddl_table_schema))) {
        LOG_WARN("init ivf center index failed", K(ret));
      }
    } else if (schema::is_vec_ivfsq8_meta_index(index_type)) {
      if (OB_FAIL(init_ivf_sq8_meta_index(ddl_table_schema))) {
        LOG_WARN("init ivf sq8 meta index failed", K(ret));
      }
    } else if (schema::is_vec_ivfpq_pq_centroid_index(index_type)) {
      if (OB_FAIL(init_ivf_pq_center_index(ddl_table_schema))) {
        LOG_WARN("init ivf pq center index", K(ret));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected index type", K(ret), K(index_type));
    }
  }
  return ret;
}

int ObVectorIndexTabletContext::init_hnsw_index(const ObDDLTableSchema &ddl_table_schema)
{
  int ret = OB_SUCCESS;
  // get data tablet id and lob tablet id
  ObLSHandle ls_handle;
  ObTabletHandle five_tablet_handle;
  ObTabletHandle data_tablet_handle;
  ObTabletBindingMdsUserData ddl_data;
  const ObIArray<ObColumnSchemaItem> &col_array = ddl_table_schema.column_items_;
  index_type_ = VIAT_MAX;
  if (OB_FAIL(MTL(ObLSService *)->get_ls(ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD))) {
    LOG_WARN("failed to get log stream", K(ret), K(ls_id_));
  } else if (OB_ISNULL(ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls should not be null", K(ret));
  } else if (OB_FAIL(ls_handle.get_ls()->get_tablet(tablet_id_, five_tablet_handle))) {
    LOG_WARN("fail to get tablet handle", K(ret), K(tablet_id_));
  } else if (FALSE_IT(ctx_.data_tablet_id_ = five_tablet_handle.get_obj()->get_data_tablet_id())) {
  } else if (OB_FAIL(ls_handle.get_ls()->get_tablet(ctx_.data_tablet_id_, data_tablet_handle))) {
    LOG_WARN("fail to get tablet handle", K(ret), K(ctx_.data_tablet_id_));
  } else if (OB_FAIL(data_tablet_handle.get_obj()->get_ddl_data(ddl_data))) {
    LOG_WARN("failed to get ddl data from tablet", K(ret), K(data_tablet_handle));
  } else {
    ctx_.lob_meta_tablet_id_ = ddl_data.lob_meta_tablet_id_;
    ctx_.lob_piece_tablet_id_ = ddl_data.lob_piece_tablet_id_;
  }
  // get vid col and vector col
  for (int64_t i = 0; OB_SUCC(ret) && i < col_array.count(); i++) {
    // version control col is not valid
    if (!col_array.at(i).is_valid_) {
    } else if (ObSchemaUtils::is_vec_hnsw_vid_column(col_array.at(i).column_flags_)) {
      vector_vid_col_idx_ = i;
    } else if (ObSchemaUtils::is_vec_hnsw_vector_column(col_array.at(i).column_flags_)) {
      vector_col_idx_ = i;
    } else if (ObSchemaUtils::is_vec_hnsw_key_column(col_array.at(i).column_flags_)) {
      vector_key_col_idx_ = i;
    } else if (ObSchemaUtils::is_vec_hnsw_data_column(col_array.at(i).column_flags_)) {
      vector_data_col_idx_ = i;
    } else if (OB_FAIL(extra_column_idx_types_.push_back(ObExtraInfoIdxType(i, col_array.at(i).col_type_)))) {
      LOG_WARN("failed to push back extra info col idx", K(ret), K(i));
    }
  }
  if (OB_SUCC(ret)) {
    if (vector_vid_col_idx_ == -1 || vector_col_idx_ == -1 || vector_key_col_idx_ == -1 || vector_data_col_idx_ == -1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get valid vector index col idx", K(ret), K(vector_col_idx_), K(vector_vid_col_idx_),
               K(vector_key_col_idx_), K(vector_data_col_idx_), K(col_array));
    }
  }
  return ret;
}

int ObVectorIndexTabletContext::init_ivf_center_index(const ObDDLTableSchema &ddl_table_schema)
{
  int ret = OB_SUCCESS;
  index_type_ = VIAT_MAX;
  const ObIArray<ObColumnSchemaItem> &col_array = ddl_table_schema.column_items_;
  for (int64_t i = 0; OB_SUCC(ret) && i < col_array.count(); i++) {
    if (ObSchemaUtils::is_vec_ivf_center_id_column(col_array.at(i).column_flags_)) {
      center_id_col_idx_ = i;
    } else if (ObSchemaUtils::is_vec_ivf_center_vector_column(col_array.at(i).column_flags_)) {
      center_vector_col_idx_ = i;
    }
  }
  if (OB_SUCC(ret)) {
    ObIvfFlatBuildHelper *helper = nullptr;
    if (center_id_col_idx_ == -1 || center_vector_col_idx_ == -1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get valid vector index col idx", K(ret), K(center_id_col_idx_), K(center_vector_col_idx_), K(col_array));
    } else if (OB_FAIL(create_ivf_build_helper(ObIndexType::INDEX_TYPE_VEC_IVFFLAT_CENTROID_LOCAL, vec_idx_param_))) {
      LOG_WARN("create ivf build helper failed", K(ret));
    } else {
      helper = static_cast<ObIvfFlatBuildHelper *>(helper_);
      if (OB_FAIL(helper->init_kmeans_ctx(vec_dim_))) {
        LOG_WARN("init kmeans ctx failed", K(ret));
      }
    }
  }
  return ret;
}

int ObVectorIndexTabletContext::init_ivf_sq8_meta_index(const ObDDLTableSchema &ddl_table_schema)
{
  int ret = OB_SUCCESS;
  index_type_ = VIAT_MAX;
  const ObIArray<ObColumnSchemaItem> &col_array = ddl_table_schema.column_items_;
  for (int64_t i = 0; OB_SUCC(ret) && i < col_array.count(); i++) {
    if (ObSchemaUtils::is_vec_ivf_meta_id_column(col_array.at(i).column_flags_)) {
      meta_id_col_idx_ = i;
    } else if (ObSchemaUtils::is_vec_ivf_meta_vector_column(col_array.at(i).column_flags_)) {
      meta_vector_col_idx_ = i;
    }
  }
  if (OB_SUCC(ret)) {
    ObIvfSq8BuildHelper *helper = nullptr;
    if (OB_FAIL(create_ivf_build_helper(ObIndexType::INDEX_TYPE_VEC_IVFSQ8_META_LOCAL, vec_idx_param_))) {
      LOG_WARN("create ivf build helper", K(ret));
    } else {
      helper = static_cast<ObIvfSq8BuildHelper *>(helper_);
      if (OB_FAIL(helper->init_ctx(vec_dim_))) {
        LOG_WARN("init result vectors failed", K(ret));
      }
    }
  }
  return ret;
}

int ObVectorIndexTabletContext::init_ivf_pq_center_index(const ObDDLTableSchema &ddl_table_schema)
{
  int ret = OB_SUCCESS;
  index_type_ = VIAT_MAX;
  const ObIArray<ObColumnSchemaItem> &col_array = ddl_table_schema.column_items_;
  for (int64_t i = 0; OB_SUCC(ret) && i < col_array.count(); i++) {
    if (ObSchemaUtils::is_vec_ivf_pq_center_id_column(col_array.at(i).column_flags_)) {
      pq_center_id_col_idx_ = i;
    } else if (ObSchemaUtils::is_vec_ivf_center_vector_column(col_array.at(i).column_flags_)) {
      pq_center_vector_col_idx_ = i;
    }
  }
  if (OB_SUCC(ret)) {
    ObIvfPqBuildHelper *helper = nullptr;
    if (pq_center_id_col_idx_ == -1 || pq_center_vector_col_idx_ == -1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get valid vector index col idx", K(ret), K(pq_center_id_col_idx_), K(pq_center_vector_col_idx_), K(col_array));
    } else if (OB_FAIL(create_ivf_build_helper(ObIndexType::INDEX_TYPE_VEC_IVFPQ_PQ_CENTROID_LOCAL, vec_idx_param_))) {
      LOG_WARN("create ivf build helper failed", K(ret));
    } else {
      helper = static_cast<ObIvfPqBuildHelper *>(helper_);
      if (OB_FAIL(helper->init_ctx(vec_dim_))) {
        LOG_WARN("failed to init kmeans ctx", K(ret));
      }
    }
  }
  return ret;
}

int ObVectorIndexTabletContext::create_ivf_build_helper(
    const ObIndexType type,
    ObString &vec_index_param)
{
  int ret = OB_SUCCESS;
  ObIvfBuildHelper *tmp_ivf_build_helper = nullptr;
  void *helper_buff = nullptr;
  if (INDEX_TYPE_VEC_IVFFLAT_CENTROID_LOCAL == type) {
    if (OB_ISNULL(helper_buff = allocator_.alloc(sizeof(ObIvfFlatBuildHelper)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for ivf index build helper", KR(ret));
    } else {
      tmp_ivf_build_helper = new(helper_buff)ObIvfFlatBuildHelper(&allocator_, tenant_id_);
      if (OB_FAIL(tmp_ivf_build_helper->init(vec_index_param, memory_context_, all_vsag_use_mem_))) {
        LOG_WARN("failed to init ivf build helper", K(ret));
      }
    }
  } else if (INDEX_TYPE_VEC_IVFSQ8_META_LOCAL == type) {
    if (OB_ISNULL(helper_buff = allocator_.alloc(sizeof(ObIvfSq8BuildHelper)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for ivf index build helper", KR(ret));
    } else {
      tmp_ivf_build_helper = new(helper_buff)ObIvfSq8BuildHelper(&allocator_, tenant_id_);
      if (OB_FAIL(tmp_ivf_build_helper->init(vec_index_param, memory_context_, all_vsag_use_mem_))) {
        LOG_WARN("failed to init ivf build helper", K(ret), K(vec_index_param));
      }
    }
  } else if (INDEX_TYPE_VEC_IVFPQ_PQ_CENTROID_LOCAL == type) {
    if (OB_ISNULL(helper_buff = allocator_.alloc(sizeof(ObIvfPqBuildHelper)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for ivf index build helper", KR(ret));
    } else {
      tmp_ivf_build_helper = new(helper_buff)ObIvfPqBuildHelper(&allocator_, tenant_id_);
      if (OB_FAIL(tmp_ivf_build_helper->init(vec_index_param, memory_context_, all_vsag_use_mem_))) {
        LOG_WARN("failed to init ivf build helper", K(ret), K(vec_index_param));
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported index type", K(ret), K(type));
  }

  if (OB_SUCC(ret)) {
    helper_ = tmp_ivf_build_helper;
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(tmp_ivf_build_helper)) {
    tmp_ivf_build_helper->~ObIvfBuildHelper();
    allocator_.free(helper_buff);
    tmp_ivf_build_helper = nullptr;
    helper_buff = nullptr;
  }
  return ret;
}

void ObVectorIndexTabletContext::destroy_ivf_build_helper()
{
  int ret = OB_SUCCESS;
  if (nullptr != helper_) {
    if (OB_FAIL(ObPluginVectorIndexUtils::release_vector_index_build_helper(helper_))) {
      LOG_ERROR("fail to release vector index adapter", KR(ret));
    }
    helper_ = nullptr;
  }
}

int ObHNSWIndexRowIterator::init(
    ObVectorIndexTabletContext &context)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    rowkey_cnt_ = context.rowkey_cnt_;
    column_cnt_ = context.column_cnt_;
    snapshot_version_ = context.snapshot_version_;
    index_type_ = context.index_type_;
    row_cnt_ = context.row_cnt_;
    vec_dim_ = context.vec_dim_;
    tablet_id_ = context.tablet_id_;
    vec_idx_param_ = context.vec_idx_param_;
    ctx_ = &context.ctx_;
    cur_row_pos_ = 0;
    vector_vid_col_idx_ = context.vector_vid_col_idx_;
    vector_col_idx_ = context.vector_col_idx_;
    vector_key_col_idx_ = context.vector_key_col_idx_;
    vector_data_col_idx_ = context.vector_data_col_idx_;
    if (OB_FAIL(extra_column_idx_types_.assign(context.extra_column_idx_types_))) {
      LOG_WARN("assign extra column idx types failed", K(ret));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

bool ObHNSWIndexRowIterator::is_vec_idx_col_invalid(const int64_t column_cnt) const
{
  return vector_key_col_idx_ < 0 || vector_key_col_idx_ >= column_cnt ||
    vector_data_col_idx_ < 0 || vector_data_col_idx_ >= column_cnt ||
    vector_vid_col_idx_ < 0 || vector_vid_col_idx_ >= column_cnt ||
    vector_col_idx_ < 0 || vector_col_idx_ >= column_cnt;
}

int ObHNSWIndexRowIterator::get_next_row(
    blocksstable::ObDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  const int64_t request_cnt = column_cnt_;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (current_row_.get_column_count() <= 0
    && OB_FAIL(current_row_.init(iter_allocator_, request_cnt))) {
    LOG_WARN("init datum row failed", K(ret), K(request_cnt));
  } else if (OB_UNLIKELY(current_row_.get_column_count() != request_cnt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(request_cnt), "datum_row_cnt", current_row_.get_column_count());
  } else if (cur_row_pos_ >= ctx_->vals_.count()) {
    ret = OB_ITER_END;
  } else if (index_type_ >= VIAT_MAX) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get index type invalid.", K(ret), K(index_type_));
  } else if (is_vec_idx_col_invalid(current_row_.get_column_count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, vec col idx error", K(ret), K(vector_key_col_idx_), K(vector_data_col_idx_),
             K(vector_vid_col_idx_), K(vector_col_idx_));
  } else {
    // set vec key
    int64_t key_pos = 0;
    row_allocator_.reuse();
    char *key_str = static_cast<char*>(row_allocator_.alloc(OB_VEC_IDX_SNAPSHOT_KEY_LENGTH));
    if (OB_ISNULL(key_str)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc vec key", K(ret));
    } else if (index_type_ == VIAT_HNSW && OB_FAIL(databuff_printf(key_str, OB_VEC_IDX_SNAPSHOT_KEY_LENGTH, key_pos, "%lu_%ld_hnsw_data_part%05ld", tablet_id_.id(), snapshot_version_, cur_row_pos_))) {
      LOG_WARN("fail to build vec snapshot key str", K(ret), K_(index_type));
    } else if (index_type_ == VIAT_HGRAPH &&
      OB_FAIL(databuff_printf(key_str, OB_VEC_IDX_SNAPSHOT_KEY_LENGTH, key_pos, "%lu_hgraph_data_part%05ld", tablet_id_.id(), cur_row_pos_))) {
      LOG_WARN("fail to build vec hgraph snapshot key str", K(ret), K_(index_type));
    } else if (index_type_ == VIAT_HNSW_SQ && OB_FAIL(databuff_printf(key_str, OB_VEC_IDX_SNAPSHOT_KEY_LENGTH, key_pos, "%lu_%ld_hnsw_sq_data_part%05ld", tablet_id_.id(), snapshot_version_, cur_row_pos_))) {
      LOG_WARN("fail to build sq vec snapshot key str", K(ret), K_(index_type));
    } else if (index_type_ == VIAT_HNSW_BQ && OB_FAIL(databuff_printf(key_str, OB_VEC_IDX_SNAPSHOT_KEY_LENGTH, key_pos, "%lu_%ld_hnsw_bq_data_part%05ld", tablet_id_.id(), snapshot_version_, cur_row_pos_))) {
      LOG_WARN("fail to build bq vec snapshot key str", K(ret), K_(index_type));
    } else if (index_type_ == VIAT_IPIVF && OB_FAIL(databuff_printf(key_str, OB_VEC_IDX_SNAPSHOT_KEY_LENGTH, key_pos, "%lu_%ld_ipivf_data_part%05ld", tablet_id_.id(), snapshot_version_, cur_row_pos_))) {
      LOG_WARN("fail to build ipivf vec snapshot key str", K(ret), K_(index_type));
    }  else {
      current_row_.storage_datums_[vector_key_col_idx_].set_string(key_str, key_pos);
    }
    // set vec data
    if (OB_FAIL(ret)) {
    } else {
      // TODO @lhd maybe we should do deep copy
      current_row_.storage_datums_[vector_data_col_idx_].set_string(ctx_->vals_.at(cur_row_pos_));
    }
    // set vid and vec to null
    if (OB_SUCC(ret)) {
      current_row_.storage_datums_[vector_vid_col_idx_].set_null();
      current_row_.storage_datums_[vector_col_idx_].set_null();
      // set extra_info to null
      if (extra_column_idx_types_.count() > 0) {
        for (int64_t i = 0; OB_SUCC(ret) && i < extra_column_idx_types_.count(); i++) {
          current_row_.storage_datums_[extra_column_idx_types_[i].idx_].set_null();
        }
      }
    }
    if (OB_SUCC(ret)) {
      // add extra rowkey
      // TODO how to get snapshot
      current_row_.storage_datums_[rowkey_cnt_].set_int(-snapshot_version_);
      current_row_.storage_datums_[rowkey_cnt_ + 1].set_int(0);
      current_row_.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
      datum_row = &current_row_;
      cur_row_pos_++;
    }
  }
  return ret;
}

int ObIVFCenterRowIterator::init(
    ObVectorIndexTabletContext &context)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    rowkey_cnt_ = context.rowkey_cnt_;
    column_cnt_ = context.column_cnt_;
    snapshot_version_ = context.snapshot_version_;
    index_type_ = context.index_type_;
    center_id_col_idx_ = context.center_id_col_idx_;
    center_vector_col_idx_ = context.center_vector_col_idx_;
    tablet_id_ = context.tablet_id_;
    lob_inrow_threshold_ = context.lob_inrow_threshold_;
    helper_ = static_cast<ObIvfFlatBuildHelper *>(context.helper_);
    vec_dim_ = context.vec_dim_;
    is_inited_ = true;
  }
  return ret;
}

int ObIVFCenterRowIterator::get_next_row(
    blocksstable::ObDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  ObSingleKmeansExecutor *executor = nullptr;
  const int64_t extra_rowkey_cnt = storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  const int64_t request_cnt = column_cnt_;
  ObIvfFlatBuildHelper *helper = helper_;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr ctx", K(ret));
  } else if (current_row_.get_column_count() <= 0
    && OB_FAIL(current_row_.init(iter_allocator_, request_cnt))) {
    LOG_WARN("init datum row failed", K(ret), K(request_cnt));
  } else if (OB_UNLIKELY(current_row_.get_column_count() != request_cnt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(request_cnt), "datum_row_cnt", current_row_.get_column_count());
  } else if (cur_row_pos_ >= executor->get_centers_count()) {
    ret = OB_ITER_END;
  } else if (center_id_col_idx_ < 0 || center_id_col_idx_ >= current_row_.get_column_count() ||
             center_vector_col_idx_ < 0 || center_vector_col_idx_ >= current_row_.get_column_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, center col idx error", K(ret), K(center_id_col_idx_), K(center_vector_col_idx_));
  } else {
    ObString data_str;
    ObString vec_res;
    float *center_vector = nullptr;
    int64_t dim = executor->get_centers_dim();
    int64_t buf_len = OB_DOC_ID_COLUMN_BYTE_LENGTH;
    char *buf = nullptr;
    row_allocator_.reuse();
    if (OB_FAIL(executor->get_center(cur_row_pos_, center_vector))) {
      LOG_WARN("upexpected nullptr center_vector", K(ret), K(cur_row_pos_));
    } else {
      data_str.assign(reinterpret_cast<char *>(center_vector), static_cast<int64_t>(sizeof(float) * dim));
      if (OB_FAIL(sql::ObArrayExprUtils::set_array_res(nullptr, data_str.length(), row_allocator_, vec_res, data_str.ptr()))) {
        LOG_WARN("failed to set array res", K(ret));
      } else if (OB_ISNULL(buf = static_cast<char*>(row_allocator_.alloc(buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc cid", K(ret));
      } else {
        ObString cid_str(buf_len, 0, buf);
        ObCenterId center_id(tablet_id_.id(), cur_row_pos_ + 1);
        if (OB_FAIL(ObVectorClusterHelper::set_center_id_to_string(center_id, cid_str))) {
          LOG_WARN("failed to set center_id to string", K(ret), K(center_id), K(cid_str));
        } else if (vec_res.length() > lob_inrow_threshold_ || cid_str.length() > lob_inrow_threshold_) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected outrow datum in ivf vector index",
                    K(ret), K(vec_res.length()), K(cid_str.length()), K(lob_inrow_threshold_));
        } else {
          for (int64_t idx = rowkey_cnt_ + extra_rowkey_cnt; idx < request_cnt; ++idx) {
            if (idx != center_id_col_idx_ && idx != center_vector_col_idx_) {
              current_row_.storage_datums_[idx].set_null(); // set null part key
            }
          }
          current_row_.storage_datums_[center_vector_col_idx_].set_string(vec_res);
          current_row_.storage_datums_[center_id_col_idx_].set_string(cid_str);
          current_row_.storage_datums_[rowkey_cnt_].set_int(-snapshot_version_);
          current_row_.storage_datums_[rowkey_cnt_ + 1].set_int(0);
          current_row_.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
          datum_row = &current_row_;
          cur_row_pos_++;
        }
      }
    }
  }
  return ret;
}

int ObIVFSq8MetaRowIterator::init(ObVectorIndexTabletContext &context)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    rowkey_cnt_ = context.rowkey_cnt_;
    column_cnt_ = context.column_cnt_;
    snapshot_version_ = context.snapshot_version_;
    meta_id_col_idx_ = context.meta_id_col_idx_;
    meta_vector_col_idx_ = context.meta_vector_col_idx_;
    tablet_id_ = context.tablet_id_;
    vec_dim_ = context.vec_dim_;
    lob_inrow_threshold_ = context.lob_inrow_threshold_;
    helper_ = static_cast<ObIvfSq8BuildHelper *>(context.helper_);
    is_inited_ = true;
  }
  return ret;
}

int ObIVFSq8MetaRowIterator::get_next_row(
    blocksstable::ObDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  const int64_t request_cnt = column_cnt_;
  ObIvfSq8BuildHelper *helper = helper_;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (current_row_.get_column_count() <= 0
    && OB_FAIL(current_row_.init(iter_allocator_, request_cnt))) {
    LOG_WARN("init datum row failed", K(ret), K(request_cnt));
  } else if (OB_UNLIKELY(current_row_.get_column_count() != request_cnt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(request_cnt), "datum_row_cnt", current_row_.get_column_count());
  } else if (cur_row_pos_ >= ObIvfConstant::SQ8_META_ROW_COUNT) {
    ret = OB_ITER_END;
  } else if (meta_id_col_idx_ < 0 || meta_id_col_idx_ >= current_row_.get_column_count() ||
             meta_vector_col_idx_ < 0 || meta_vector_col_idx_ >= current_row_.get_column_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, center col idx error", K(ret), K(meta_id_col_idx_), K(meta_vector_col_idx_));
  } else {
    ObString data_str;
    ObString vec_res;
    float *cur_vector = nullptr;
    int64_t buf_len = OB_DOC_ID_COLUMN_BYTE_LENGTH;
    char *buf = nullptr;
    row_allocator_.reuse();
    if (OB_FAIL(helper->get_result(cur_row_pos_, cur_vector))) {
      LOG_WARN("fail to get result", K(ret));
    } else {
      data_str.assign(reinterpret_cast<char *>(cur_vector), static_cast<int64_t>(sizeof(float) * vec_dim_));
      if (OB_FAIL(sql::ObArrayExprUtils::set_array_res(nullptr, data_str.length(), row_allocator_, vec_res, data_str.ptr()))) {
        LOG_WARN("failed to set array res", K(ret));
      } else if (OB_ISNULL(buf = static_cast<char*>(row_allocator_.alloc(buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc cid", K(ret));
      } else {
        ObString cid_str(buf_len, 0, buf);
        // reuse center_id encode, min: 1, max: 2, step: 3
        ObCenterId center_id(tablet_id_.id(), cur_row_pos_ + 1);
        if (OB_FAIL(ObVectorClusterHelper::set_center_id_to_string(center_id, cid_str))) {
          LOG_WARN("failed to set center_id to string", K(ret), K(center_id), K(cid_str));
        } else if (vec_res.length() > lob_inrow_threshold_ || cid_str.length() > lob_inrow_threshold_) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected outrow datum in ivf vector index",
                    K(ret), K(vec_res.length()), K(cid_str.length()), K(lob_inrow_threshold_));
        } else {
          for (int64_t i = 0; i < current_row_.get_column_count(); ++i) {
            if (meta_vector_col_idx_ == i) {
              current_row_.storage_datums_[meta_vector_col_idx_].set_string(vec_res);
            } else if (meta_id_col_idx_ == i) {
              current_row_.storage_datums_[meta_id_col_idx_].set_string(cid_str);
            } else if (rowkey_cnt_ == i) {
              current_row_.storage_datums_[i].set_int(-snapshot_version_);
            } else if (rowkey_cnt_ + 1 == i) {
              current_row_.storage_datums_[i].set_int(0);
            } else {
              current_row_.storage_datums_[i].set_null(); // set part key null
            }
          }
          current_row_.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
          datum_row = &current_row_;
          cur_row_pos_++;
        }
      }
    }
  }
  return ret;
}

int ObIVFPqRowIterator::init(
    ObVectorIndexTabletContext &context)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    rowkey_cnt_ = context.rowkey_cnt_;
    column_cnt_ = context.column_cnt_;
    snapshot_version_ = context.snapshot_version_;
    pq_center_vector_col_idx_ = context.pq_center_vector_col_idx_;
    pq_center_id_col_idx_ = context.pq_center_id_col_idx_;
    vec_dim_ = context.vec_dim_;
    helper_ = static_cast<ObIvfPqBuildHelper *>(context.helper_);
    tablet_id_ = context.tablet_id_;
    lob_inrow_threshold_ = context.lob_inrow_threshold_;
    is_inited_ = true;
  }
  return ret;
}

int ObIVFPqRowIterator::get_next_row(
    blocksstable::ObDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  ObMultiKmeansExecutor *executor = nullptr;
  const int64_t request_cnt = column_cnt_;
  ObIvfPqBuildHelper *helper = helper_;
  if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr ctx", K(ret));
  } else if (current_row_.get_column_count() <= 0
    && OB_FAIL(current_row_.init(iter_allocator_, request_cnt))) {
    LOG_WARN("init datum row failed", K(ret), K(request_cnt));
  } else if (OB_UNLIKELY(current_row_.get_column_count() != request_cnt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(request_cnt), "datum_row_cnt", current_row_.get_column_count());
  } else if (cur_row_pos_ >= executor->get_total_centers_count()) {
    ret = OB_ITER_END;
  } else if (pq_center_id_col_idx_ < 0 || pq_center_id_col_idx_ >= current_row_.get_column_count() ||
             pq_center_vector_col_idx_ < 0 || pq_center_vector_col_idx_ >= current_row_.get_column_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, center col idx error", K(ret), K(pq_center_id_col_idx_), K(pq_center_vector_col_idx_));
  } else {
    ObString data_str;
    ObString vec_res;
    float *center_vector = nullptr;
    int64_t dim = executor->get_centers_dim();
    int64_t buf_len = OB_DOC_ID_COLUMN_BYTE_LENGTH;
    char *buf = nullptr;
    int64_t center_count_per_kmeans = executor->get_centers_count_per_kmeans();
    row_allocator_.reuse();
    if (center_count_per_kmeans == 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("upexpected zero center count", K(ret), K(center_count_per_kmeans));
    } else if (OB_FAIL(executor->get_center(cur_row_pos_, center_vector))) {
      LOG_WARN("upexpected nullptr center_vector", K(ret), K(cur_row_pos_), K(center_count_per_kmeans));
    } else {
      data_str.assign(reinterpret_cast<char *>(center_vector), static_cast<int64_t>(sizeof(float) * dim));
      if (OB_FAIL(sql::ObArrayExprUtils::set_array_res(nullptr, data_str.length(), row_allocator_, vec_res, data_str.ptr()))) {
        LOG_WARN("failed to set array res", K(ret));
      } else if (OB_ISNULL(buf = static_cast<char*>(row_allocator_.alloc(buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc cid", K(ret));
      } else {
        ObString pq_cid_str(buf_len, 0, buf);
        // row_i = pq_centers[m_id - 1][center_id - 1] since m_id and center_id start from 1
        ObPqCenterId pq_center_id(tablet_id_.id(), cur_row_pos_ / center_count_per_kmeans + 1, cur_row_pos_ % center_count_per_kmeans + 1);
        if (OB_FAIL(ObVectorClusterHelper::set_pq_center_id_to_string(pq_center_id, pq_cid_str))) {
          LOG_WARN("failed to set center_id to string", K(ret), K(pq_center_id), K(pq_cid_str));
        } else if (vec_res.length() > lob_inrow_threshold_ || pq_cid_str.length() > lob_inrow_threshold_) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected outrow datum in ivf vector index",
                    K(ret), K(vec_res.length()), K(pq_cid_str.length()), K(lob_inrow_threshold_));
        } else {
          for (int64_t i = 0; i < current_row_.get_column_count(); ++i) {
            if (pq_center_vector_col_idx_ == i) {
              current_row_.storage_datums_[i].set_string(vec_res);
            } else if (pq_center_id_col_idx_ == i) {
              current_row_.storage_datums_[i].set_string(pq_cid_str);
            } else if (rowkey_cnt_ == i) {
              current_row_.storage_datums_[i].set_int(-snapshot_version_);
            } else if (rowkey_cnt_ + 1 == i) {
              current_row_.storage_datums_[i].set_int(0);
            } else {
              current_row_.storage_datums_[i].set_null(); // set part key null
            }
          }
          current_row_.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
          datum_row = &current_row_;
          cur_row_pos_++;
        }
      }
    }
  }
  return ret;
}

ObVectorIndexBaseOperator::ObVectorIndexBaseOperator(ObPipeline *pipeline)
  : ObPipelineOperator(pipeline), is_inited_(false), tablet_id_(), slice_idx_(0),
    op_allocator_("VecIndexOp", OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID()),
    row_allocator_("VecIndexRow", OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID())
{
}

int ObVectorIndexBaseOperator::init(const ObTabletID &tablet_id, const int64_t slice_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() || slice_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(slice_idx));
  } else {
    tablet_id_ = tablet_id;
    slice_idx_ = slice_idx;
    is_inited_ = true;
  }
  return ret;
}

bool ObVectorIndexBaseOperator::is_valid() const
{
  return tablet_id_.is_valid();
}

int ObIVFIndexBaseOperator::init(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObDDLTabletContext *tablet_context = nullptr;
  tablet_id_ = tablet_id;
  if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id));
  } else if (OB_FAIL(get_ddl_tablet_context(tablet_context))) {
    LOG_WARN("get ddl tablet context failed", K(ret));
  } else {
    helper_ = tablet_context->vector_index_ctx_->helper_;
    is_inited_ = true;
  }
  return ret;
}

int ObVectorIndexBaseOperator::get_ddl_tablet_context(ObDDLTabletContext *&tablet_context)
{
  int ret = OB_SUCCESS;
  ObDDLIndependentDag *dag = nullptr;
  tablet_context = nullptr;
  if (OB_ISNULL(get_dag())) {
    ret = OB_ERR_SYS;
    LOG_WARN("get dag failed", K(ret));
  } else if (OB_FALSE_IT(dag = static_cast<ObDDLIndependentDag *>(get_dag()))) {
  } else if (OB_FAIL(dag->get_tablet_context(tablet_id_, tablet_context))) {
    LOG_WARN("get tablet context failed", K(ret));
  } else if (OB_ISNULL(tablet_context)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, invalid tablet context", K(ret));
  }
  return ret;
}

int ObHNSWIndexAppendBufferOperator::init(
    const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObDDLTabletContext *tablet_context = nullptr;
  ObVectorIndexTabletContext *vector_index_ctx = nullptr;
  tablet_id_ = tablet_id;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (OB_FAIL(get_ddl_tablet_context(tablet_context))) {
    LOG_WARN("get ddl tablet context failed", K(ret), K(tablet_id));
  } else if (OB_ISNULL(vector_index_ctx = tablet_context->vector_index_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, vector index ctx is null", K(ret));
  } else {
    is_inited_ = true;
    vec_idx_param_ = vector_index_ctx->vec_idx_param_;
    vec_dim_ = vector_index_ctx->vec_dim_;
    vector_vid_col_idx_ = vector_index_ctx->vector_vid_col_idx_;
    vector_col_idx_ = vector_index_ctx->vector_col_idx_;
    vector_key_col_idx_ = vector_index_ctx->vector_key_col_idx_;
    vector_data_col_idx_ = vector_index_ctx->vector_data_col_idx_;
    if (OB_FAIL(extra_column_idx_types_.assign(vector_index_ctx->extra_column_idx_types_))) {
      LOG_WARN("assign extra column idx types failed", K(ret));
    }
  }
  return ret;
}

int ObHNSWIndexAppendBufferOperator::append_row(
    const int64_t row_pos,
    const common::ObIArray<common::ObIVector *> &vectors,
    ObDDLTabletContext *tablet_context)
{
  int ret = OB_SUCCESS;
  // get vid and vector
  ObString vec_str;
  int64_t vec_vid;
  ObVecExtraInfoObj *extra_obj = nullptr;
  int64_t extra_column_count = extra_column_idx_types_.count();
  int64_t extra_info_actual_size = 0;
  row_allocator_.reuse();
  if (vectors.count() <= vector_vid_col_idx_ || vectors.count() <= vector_col_idx_ || row_pos < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get valid vector index col idx", K(ret), K(vector_col_idx_), K(vector_vid_col_idx_), K(row_pos));
  } else if (vectors.at(vector_col_idx_)->is_null(row_pos)) {
    // do nothing
  } else if (FALSE_IT(vec_vid = vectors.at(vector_vid_col_idx_)->get_int(row_pos))) {
  } else if (FALSE_IT(vec_str = vectors.at(vector_col_idx_)->get_string(row_pos))) {
  } else if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(&row_allocator_,
                                                                ObLongTextType,
                                                                CS_TYPE_BINARY,
                                                                true,
                                                                vec_str))) {
    LOG_WARN("fail to get real data.", K(ret), K(vec_str));
  } else if (vec_str.length() == 0) {
    // do nothing
  } else {
    ObPluginVectorIndexService *vec_index_service = MTL(ObPluginVectorIndexService *);
    ObPluginVectorIndexAdapterGuard adaptor_guard;
    if (OB_ISNULL(vec_index_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, vector index service is nullptr", K(ret));
    } else if (OB_FAIL(vec_index_service->acquire_adapter_guard(
                   tablet_context->vector_index_ctx_->ls_id_, tablet_id_,
                   ObIndexType::INDEX_TYPE_VEC_INDEX_SNAPSHOT_DATA_LOCAL, adaptor_guard,
                   &tablet_context->vector_index_ctx_->vec_idx_param_, tablet_context->vector_index_ctx_->vec_dim_))) {
      LOG_WARN("fail to get ObMockPluginVectorIndexAdapter", K(ret), K(tablet_context->vector_index_ctx_->ls_id_),
               K(tablet_id_));
    } else if (OB_NOT_NULL(adaptor_guard.get_adatper()) &&
               OB_FAIL(adaptor_guard.get_adatper()->get_extra_info_actual_size(extra_info_actual_size))) {
      LOG_WARN("failed to get extra info actual size.", K(ret));
    } else if (extra_column_count > 0 && extra_info_actual_size > 0) {
      char *buf = nullptr;
      if (OB_ISNULL(buf = static_cast<char *>(row_allocator_.alloc(sizeof(ObVecExtraInfoObj) * extra_column_count)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret), K(extra_column_count));
      } else if (OB_FALSE_IT(extra_obj = new (buf) ObVecExtraInfoObj[extra_column_count])) {
      }
      int64_t datum_row_count = vectors.count();
      for (int64_t i = 0; OB_SUCC(ret) && i < extra_column_count; ++i) {
        if (datum_row_count <= extra_column_idx_types_.at(i).idx_) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to get valid extra_info idx", K(ret), K(extra_column_idx_types_.at(i).idx_), K(datum_row_count));
        } else {
          const ObIVector &extra_vector = *vectors.at(extra_column_idx_types_.at(i).idx_);
          if (OB_FAIL(extra_obj[i].from_vector(extra_vector, row_pos, extra_column_idx_types_.at(i).type_, &row_allocator_))) {
            LOG_WARN("failed to from obj.", K(ret), K(extra_column_idx_types_), K(i));
          }
        }
      }
    }
    uint32_t vec_length = vec_str.length();
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(adaptor_guard.get_adatper()->add_snap_index(reinterpret_cast<float *>(vec_str.ptr()),
                                                                   &vec_vid, extra_obj, extra_column_count, 1, &vec_length))) {
      LOG_WARN("fail to build index to adaptor", K(ret), KPC(this));
    } else {
      LOG_DEBUG("[vec index debug] add into snap index success", K(tablet_id_), K(vec_vid), K(vec_str));
    }
  }
  return ret;
}

int ObHNSWIndexAppendBufferOperator::append_row_file(ObCGRowFile *row_file, ObDDLTabletContext *tablet_context)
{
  int ret = OB_SUCCESS;
  ObBatchDatumRows *datum_rows = nullptr;
  if (OB_ISNULL(row_file) || OB_ISNULL(tablet_context)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(row_file), KP(tablet_context));
  }
  while (OB_SUCC(ret)) {
    if (OB_FAIL(row_file->get_next_batch(datum_rows))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("get next batch failed", K(ret));
      }
    } else {
      const ObArray<common::ObIVector *> &vectors = datum_rows->vectors_;
      const int64_t total_row_count = datum_rows->row_count_;
      for (int64_t i = 0; OB_SUCC(ret) && i < total_row_count; ++i) {
        if (OB_FAIL(append_row(i, vectors, tablet_context))) {
          LOG_WARN("append row failed", K(ret), K(i));
        }
      }
    }
  }
  return ret;
}

int ObHNSWIndexAppendBufferOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (input_chunk.is_end_chunk()) {
    // do nothing
  } else {
    ObDDLIndependentDag *dag = nullptr;
    ObDDLTabletContext *tablet_context = nullptr;
    if (OB_FAIL(get_ddl_tablet_context(tablet_context))) {
      LOG_WARN("get ddl tablet context failed", K(ret));
    } else if (OB_ISNULL(tablet_context->vector_index_ctx_)) {
      ret = OB_ERR_SYS;
      LOG_WARN("error sys, invalid vector index ctx", K(ret));
    } else {
      if (OB_UNLIKELY(!input_chunk.is_valid() || !input_chunk.is_cg_row_tmp_files_type())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid arguments", K(ret), K(input_chunk));
      } else {
        ObArray<ObCGRowFile *> *cg_row_file_arr = input_chunk.cg_row_file_arr_;
        for (int64_t i = 0; OB_SUCC(ret) && i < cg_row_file_arr->count(); ++i) {
          ObCGRowFile *&row_file = cg_row_file_arr->at(i);
          if (OB_UNLIKELY(nullptr == row_file)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("error unexpected, cg row file is nullptr", K(ret), K(*cg_row_file_arr));
          } else if (OB_FAIL(append_row_file(row_file, tablet_context))) {
            LOG_WARN("append row file failed", K(ret));
          }
          if (nullptr != row_file) {
            row_file->~ObCGRowFile();
            ob_free(row_file);
            row_file = nullptr;
          }
        }
      }
    }
  }
  return ret;
}

int ObHNSWIndexBuildOperator::init(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  tablet_id_ = tablet_id;
  is_inited_ = true;
  return ret;
}

int ObHNSWIndexBuildOperator::serialize_vector_index(
    ObIAllocator *allocator,
    transaction::ObTxDesc *tx_desc,
    int64_t lob_inrow_threshold,
    ObVectorIndexAlgorithmType &type,
    ObVectorIndexTabletContext &ctx)
{
  int ret = OB_SUCCESS;
  // first we do vsag serialize
  ObPluginVectorIndexService *vec_index_service = MTL(ObPluginVectorIndexService *);
  ObPluginVectorIndexAdapterGuard adaptor_guard;
  row_allocator_.reuse();
  if (OB_ISNULL(vec_index_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get null ObPluginVectorIndexService ptr", K(ret), K(MTL_ID()));
  } else if (OB_FAIL(vec_index_service->acquire_adapter_guard(ctx.ls_id_,
                                                              tablet_id_,
                                                              ObIndexType::INDEX_TYPE_VEC_INDEX_SNAPSHOT_DATA_LOCAL,
                                                              adaptor_guard,
                                                              &ctx.vec_idx_param_,
                                                              ctx.vec_dim_))) {
    LOG_WARN("fail to get ObMockPluginVectorIndexAdapter", K(ret), K(ctx.ls_id_), K(tablet_id_));
  } else {
    ObHNSWSerializeCallback callback;
    ObOStreamBuf::Callback cb = callback;

    ObHNSWSerializeCallback::CbParam param;
    param.vctx_ = &ctx.ctx_;
    param.allocator_ = allocator;
    param.tmp_allocator_ = &row_allocator_;
    param.lob_inrow_threshold_ = lob_inrow_threshold;
    // build tx
    oceanbase::transaction::ObTransService *txs = MTL(transaction::ObTransService*);
    oceanbase::transaction::ObTxReadSnapshot snapshot;
    int64_t timeout = ObTimeUtility::fast_current_time() + storage::ObInsertLobColumnHelper::LOB_TX_TIMEOUT;
    if (OB_ISNULL(tx_desc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get tx desc, get nullptr", K(ret));
    } else if (OB_FAIL(txs->get_ls_read_snapshot(*tx_desc, transaction::ObTxIsolationLevel::RC, ctx.ls_id_, timeout, snapshot))) {
      LOG_WARN("fail to get snapshot", K(ret));
    } else {
      param.timeout_ = timeout;
      param.snapshot_ = &snapshot;
      param.tx_desc_ = tx_desc;
      ObPluginVectorIndexAdaptor *adp = adaptor_guard.get_adatper();
      if (OB_FAIL(adp->check_snap_hnswsq_index())) {
        LOG_WARN("failed to check snap hnswsq index", K(ret));
      } else if (OB_FAIL(adp->serialize(&row_allocator_, param, cb))) {
        if (OB_NOT_INIT == ret) {
          // ignore // no data in slice store
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("fail to do vsag serialize", K(ret));
        }
      } else {
        type = adp->get_snap_index_type();
        ctx.index_type_ = type;
        LOG_INFO("HgraphIndex finish vsag serialize for tablet", K(tablet_id_), K(ctx.ctx_.get_vals().count()), K(type));
      }
    }
  }
  row_allocator_.reuse();
  return ret;
}

int ObHNSWIndexBuildOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  int end_trans_ret = OB_SUCCESS;
  ObDDLTabletContext *tablet_context = nullptr;
  output_chunk.reset();
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (input_chunk.is_end_chunk()) {
    // do nothing
  } else if (OB_FAIL(input_chunk.get_dag_tablet_context(tablet_context))) {
    LOG_WARN("get ddl tablet context failed", K(ret));
  } else {
    ObVectorIndexAlgorithmType index_type = VIAT_MAX;
    const uint64_t timeout_us = ObTimeUtility::current_time() + storage::ObInsertLobColumnHelper::LOB_TX_TIMEOUT;
    transaction::ObTxDesc *tx_desc = nullptr;
    if (OB_FAIL(ObInsertLobColumnHelper::start_trans(tablet_context->ls_id_, false/*is_for_read*/, timeout_us, tx_desc))) {
      LOG_WARN("fail to get tx_desc", K(ret));
    } else if (OB_FAIL(serialize_vector_index(&op_allocator_, tx_desc, tablet_context->vector_index_ctx_->lob_inrow_threshold_, index_type, *tablet_context->vector_index_ctx_))) {
      LOG_WARN("serialize vector index failed", K(ret));
    }
    if (OB_NOT_NULL(tx_desc)) {
      if (OB_SUCCESS != (end_trans_ret = storage::ObInsertLobColumnHelper::end_trans(tx_desc, OB_SUCCESS != ret, INT64_MAX))) {
        LOG_WARN("fail to end read trans", K(ret), K(end_trans_ret));
        ret = end_trans_ret;
      }
    }
    if (OB_SUCC(ret)) {
      output_chunk.type_ = ObChunk::DAG_TABLET_CONTEXT;
      output_chunk.data_ptr_ = input_chunk.data_ptr_;
    }
  }
  return ret;
}

int ObVectorIndexWriteMacroBaseOperator::init(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  tablet_id_ = tablet_id;
  is_inited_ = true;
  return ret;
}

int ObVectorIndexWriteMacroBaseOperator::write(const ObChunk &input_chunk, ObVectorIndexRowIterator &iter)
{
  int ret = OB_SUCCESS;
  ObDDLTabletContext *tablet_context = nullptr;
  if (input_chunk.is_end_chunk()) {
    // do nothing
  } else if (OB_FAIL(input_chunk.get_dag_tablet_context(tablet_context))) {
    LOG_WARN("get ddl tablet context failed", K(ret));
  } else {
    ObTabletSliceWriter *slice_writer = nullptr;
    if (OB_FAIL(iter.init(*tablet_context->vector_index_ctx_))) {
      LOG_WARN("fail to init iterator", K(ret));
    } else {
      int ret = OB_SUCCESS;
      blocksstable::ObDatumRow *datum_row = nullptr;
      ObWriteMacroParam write_param;
      ObDDLIndependentDag *ddl_dag = nullptr;
      if (OB_ISNULL(slice_writer = OB_NEWx(ObTabletSliceWriter, &op_allocator_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory for tablet slice writer failed", K(ret));
      } else if (OB_ISNULL(ddl_dag = static_cast<ObDDLIndependentDag *>(get_dag()))) {
        ret = OB_ERR_SYS;
        LOG_WARN("get dag failed", K(ret));
      } else if (OB_FAIL(ObDDLUtil::fill_writer_param(tablet_id_, slice_idx_, -1/*cg_idx*/, ddl_dag, 0/*max_batch_size*/, write_param))) {
        LOG_WARN("fill writer param failed", K(ret));
      } else if (OB_FAIL(slice_writer->init(write_param))) {
        LOG_WARN("init macro block slice store failed", K(ret));
      } else {
        // do write
        while (OB_SUCC(ret)) {
          // build row
          if (OB_FAIL(iter.get_next_row(datum_row))) {
            if (ret != OB_ITER_END) {
              LOG_WARN("fail to get next vector data row", K(ret));
            }
          } else if (OB_FAIL(slice_writer->append_row(*datum_row))) {
            LOG_WARN("fail to append row to macro block slice store", K(ret));
          } else {
            /*if (OB_NOT_NULL(insert_monitor)) {
              insert_monitor->inserted_row_cnt_ =  insert_monitor->inserted_row_cnt_ + 1;
            }*/
          }
        }
        if (ret == OB_ITER_END) {
          ret = OB_SUCCESS;
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(slice_writer->close())) {
            LOG_WARN("fail to close macro_block_slice_store", K(ret));
          }
        }
      }
    }
    if (OB_NOT_NULL(slice_writer)) {
      slice_writer->~ObTabletSliceWriter();
      slice_writer = nullptr;
    }
  }
  return ret;
}

int ObHNSWIndexWriteMacroOperator::init(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  tablet_id_ = tablet_id;
  is_inited_ = true;
  return ret;
}

int ObHNSWIndexWriteMacroOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  output_chunk.reset();
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_FAIL(write(input_chunk, iter_))) {
    LOG_WARN("write macro failed", K(ret));
  }
  return ret;
}

int ObIVFIndexAppendBufferBaseOperator::append_row_file(ObCGRowFile *row_file)
{
  int ret = OB_SUCCESS;
  ObBatchDatumRows *datum_rows = nullptr;
  if (OB_ISNULL(row_file)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(row_file));
  }
  while (OB_SUCC(ret)) {
    if (OB_FAIL(row_file->get_next_batch(datum_rows))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("get next batch failed", K(ret));
      }
    } else {
      const ObArray<common::ObIVector *> &vectors = datum_rows->vectors_;
      const int64_t total_row_count = datum_rows->row_count_;
      for (int64_t i = 0; OB_SUCC(ret) && i < total_row_count; ++i) {
        if (OB_FAIL(append_row(i, *vectors.at(vector_col_idx_)))) {
          LOG_WARN("append row failed", K(ret), K(i));
        }
      }
    }
  }
  return ret;
}

int ObIVFIndexAppendBufferBaseOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (input_chunk.is_end_chunk()) {
    // do nothing
  } else if (OB_UNLIKELY(!input_chunk.is_valid() || !input_chunk.is_cg_row_tmp_files_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(input_chunk));
  } else {
    ObArray<ObCGRowFile *> *cg_row_file_arr = input_chunk.cg_row_file_arr_;
    for (int64_t i = 0; OB_SUCC(ret) && i < cg_row_file_arr->count(); ++i) {
      ObCGRowFile *&row_file = cg_row_file_arr->at(i);
      if (OB_UNLIKELY(nullptr == row_file)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, cg row file is nullptr", K(ret), K(*cg_row_file_arr));
      } else if (OB_FAIL(append_row_file(row_file))) {
        LOG_WARN("append row file failed", K(ret));
      }
      if (nullptr != row_file) {
        row_file->~ObCGRowFile();
        ob_free(row_file);
        row_file = nullptr;
      }
    }
  }
  return ret;
}

int ObIVFCenterAppendBufferOperator::init(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObDDLTabletContext *tablet_context = nullptr;
  ObVectorIndexTabletContext *vector_index_ctx = nullptr;
  tablet_id_ = tablet_id;
  if (OB_FAIL(ObIVFIndexBaseOperator::init(tablet_id))) {
    LOG_WARN("init ivf base operator failed", K(ret));
  } else if (OB_FAIL(get_ddl_tablet_context(tablet_context))) {
    LOG_WARN("get ddl tablet context failed", K(ret));
  } else if (OB_ISNULL(vector_index_ctx = tablet_context->vector_index_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, vector index ctx is null", K(ret));
  } else {
    vector_col_idx_ = vector_index_ctx->center_vector_col_idx_;
    is_inited_ = true;
  }
  return ret;
}

int ObIVFCenterAppendBufferOperator::append_row(
    const int64_t row_pos,
    const ObIVector &vector)
{
  int ret = OB_SUCCESS;
  row_allocator_.reuse();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    // get vid and vector
    ObString vec_str;
    ObSingleKmeansExecutor *executor = nullptr;
    ObIvfFlatBuildHelper *helper = nullptr;
    if (vector.is_null(row_pos)) {
      // do nothing // ignore
    } else if (FALSE_IT(vec_str = vector.get_string(row_pos))) {
    } else if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(&row_allocator_,
                                                                  ObLongTextType,
                                                                  CS_TYPE_BINARY,
                                                                  true,
                                                                  vec_str))) {
      LOG_WARN("fail to get real data.", K(ret), K(vec_str), K(vector.get_string(row_pos)), K(row_pos));
    } else if (OB_FAIL(get_spec_ivf_helper<ObIvfFlatBuildHelper>(helper_, helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr ctx", K(ret));
    } else if (OB_FAIL(executor->append_sample_vector(reinterpret_cast<float*>(vec_str.ptr())))) {
      LOG_WARN("failed to append sample vector", K(ret));
    } else {
      LOG_DEBUG("[vec index debug] append sample vector", K(tablet_id_), K(vec_str));
    }
  }
  return ret;
}

int ObIVFCenterIndexBuildOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  ObDDLTabletContext *tablet_context = nullptr;
  output_chunk.reset();
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (input_chunk.is_end_chunk()) {
    // do nothing
  } else if (OB_FAIL(input_chunk.get_dag_tablet_context(tablet_context))) {
    LOG_WARN("get dag tablet context failed", K(ret));
  } else {
    ObSingleKmeansExecutor *executor = nullptr;
    ObIvfFlatBuildHelper *helper = nullptr;
    if (OB_FAIL(get_spec_ivf_helper(helper_, helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr ctx", K(ret));
    } else if (OB_FAIL(executor->build())) {
      LOG_WARN("failed to build clusters", K(ret));
    } else {
      output_chunk = input_chunk;
    }
  }
  return ret;
}

int ObIVFCenterWriteMacroOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  output_chunk.reset();
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_FAIL(write(input_chunk, iter_))) {
    LOG_WARN("write macro failed", K(ret));
  }
  return ret;
}

int ObIVFSq8MetaAppendBufferOperator::init(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObDDLTabletContext *tablet_context = nullptr;
  ObVectorIndexTabletContext *vector_index_ctx = nullptr;
  tablet_id_ = tablet_id;
  if (OB_FAIL(ObIVFIndexBaseOperator::init(tablet_id))) {
    LOG_WARN("init ivf index base operator failed", K(ret));
  } else if (OB_FAIL(get_ddl_tablet_context(tablet_context))) {
    LOG_WARN("get ddl tablet context failed", K(ret));
  } else if (OB_ISNULL(vector_index_ctx = tablet_context->vector_index_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, vector index ctx is null", K(ret));
  } else {
    vector_col_idx_ = vector_index_ctx->meta_vector_col_idx_;
    is_inited_ = true;
  }
  return ret;
}

int ObIVFSq8MetaAppendBufferOperator::append_row(
    const int64_t row_pos,
    const ObIVector &vector)
{
  int ret = OB_SUCCESS;
  row_allocator_.reuse();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    // get vid and vector
    ObString vec_str;
    ObSingleKmeansExecutor *ctx = nullptr;
    ObIvfSq8BuildHelper *helper = nullptr;
    int64_t vec_dim = 0;
    if (vector.is_null(row_pos)) {
      // do nothing // ignore
    } else if (FALSE_IT(vec_str = vector.get_string(row_pos))) {
    } else if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(&row_allocator_,
                                                                  ObLongTextType,
                                                                  CS_TYPE_BINARY,
                                                                  true,
                                                                  vec_str))) {
      LOG_WARN("fail to get real data.", K(ret), K(vec_str));
    } else if (OB_FAIL(get_spec_ivf_helper<ObIvfSq8BuildHelper>(helper_, helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (FALSE_IT(vec_dim = vec_str.length() / sizeof(float))) {
    } else if (OB_FAIL(helper->update(reinterpret_cast<float*>(vec_str.ptr()), vec_dim))) {
      LOG_WARN("failed to update helper", K(ret));
    } else {
      LOG_DEBUG("[vec index debug] append sample vector", K(tablet_id_), K(vec_str));
    }
  }
  return ret;
}

int ObIVFSq8MetaIndexBuildOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  ObDDLTabletContext *tablet_context = nullptr;
  output_chunk.reset();
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (input_chunk.is_end_chunk()) {
    // do nothing
  } else if (OB_FAIL(input_chunk.get_dag_tablet_context(tablet_context))) {
    LOG_WARN("get dag tablet context failed", K(ret));
  } else {
    ObSingleKmeansExecutor *executor = nullptr;
    ObIvfSq8BuildHelper *helper = nullptr;
    if (OB_FAIL(get_spec_ivf_helper<ObIvfSq8BuildHelper>(helper_, helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_FAIL(helper->build())) {
      LOG_WARN("fail to do helper build", K(ret), KPC(helper));
    } else {
      output_chunk = input_chunk;
    }
  }
  return ret;
}

int ObIVFSq8MetaWriteMacroOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  output_chunk.reset();
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_FAIL(write(input_chunk, iter_))) {
    LOG_WARN("write macro failed", K(ret));
  }
  return ret;
}

int ObIVFPqAppendBufferOperator::init(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObDDLTabletContext *tablet_context = nullptr;
  ObVectorIndexTabletContext *vector_index_ctx = nullptr;
  tablet_id_ = tablet_id;
  if (OB_FAIL(ObIVFIndexBaseOperator::init(tablet_id))) {
    LOG_WARN("init ivf index base operator failed", K(ret));
  } else if (OB_FAIL(get_ddl_tablet_context(tablet_context))) {
    LOG_WARN("get ddl tablet context failed", K(ret));
  } else if (OB_ISNULL(vector_index_ctx = tablet_context->vector_index_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, vector index ctx is null", K(ret));
  } else {
    vector_col_idx_ = vector_index_ctx->pq_center_vector_col_idx_;
    is_inited_ = true;
  }
  return ret;
}

int ObIVFPqAppendBufferOperator::append_row(
    const int64_t row_pos,
    const ObIVector &vector)
{
  int ret = OB_SUCCESS;
  row_allocator_.reuse();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObString residual_str;
    ObMultiKmeansExecutor *executor = nullptr;
    ObIvfPqBuildHelper *helper = nullptr;
    if (vector.is_null(row_pos)) {
      // do nothing // ignore
    } else if (FALSE_IT(residual_str = vector.get_string(row_pos))) {
    } else if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(&row_allocator_,
                                                                  ObLongTextType,
                                                                  CS_TYPE_BINARY,
                                                                  true,
                                                                  residual_str))) {
      LOG_WARN("fail to get real data.", K(ret), K(residual_str));
    } else if (OB_FAIL(get_spec_ivf_helper(helper_, helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr ctx", K(ret));
    } else if (OB_FAIL(executor->append_sample_vector(reinterpret_cast<float*>(residual_str.ptr())))) {
      LOG_WARN("failed to append sample vector", K(ret));
    } else {
      LOG_DEBUG("[vec index debug] append sample vector", K(tablet_id_), K(residual_str));
    }
  }
  return ret;
}

int ObIVFPqIndexBuildOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  ObDDLTabletContext *tablet_context = nullptr;
  output_chunk.reset();
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (input_chunk.is_end_chunk()) {
    // do nothing
  } else if (OB_FAIL(input_chunk.get_dag_tablet_context(tablet_context))) {
    LOG_WARN("get dag tablet context failed", K(ret));
  } else {
    ObMultiKmeansExecutor *executor = nullptr;
    ObIvfPqBuildHelper *helper = nullptr;
    if (OB_FAIL(get_spec_ivf_helper<ObIvfPqBuildHelper>(helper_, helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr ctx", K(ret));
    } else if (OB_FAIL(executor->build(nullptr/*insert_monitor*/))) {
      LOG_WARN("failed to build clusters", K(ret));
    } else {
      output_chunk = input_chunk;
    }
  }
  return ret;
}

int ObIVFPqWriteMacroOperator::execute(
    const ObChunk &input_chunk,
    ResultState &result_state,
    ObChunk &output_chunk)
{
  int ret = OB_SUCCESS;
  output_chunk.reset();
  result_state = ObPipelineOperator::NEED_MORE_INPUT;
  if (OB_FAIL(write(input_chunk, iter_))) {
    LOG_WARN("write macro failed", K(ret));
  }
  return ret;
}
