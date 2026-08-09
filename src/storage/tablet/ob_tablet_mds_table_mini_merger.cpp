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

#include "storage/tablet/ob_tablet_mds_table_mini_merger.h"
#include "storage/tablet/ob_mds_schema_helper.h"

#define USING_LOG_PREFIX MDS

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::blocksstable;

namespace oceanbase
{
namespace storage
{

ObMdsMergeMultiVersionRowStore::ObMdsMergeMultiVersionRowStore()
  : data_store_desc_(nullptr),
    macro_writer_(nullptr),
    row_queue_allocator_(common::ObMemAttr("MdsMVRowStore")),
    shadow_row_(),
    cur_key_(),
    last_key_(),
    row_queue_(),
    is_inited_(false)
{
}

int ObMdsMergeMultiVersionRowStore::init(const ObDataStoreDesc &data_store_desc, blocksstable::ObMacroBlockWriter &macro_writer)
{
  int ret = OB_SUCCESS;
  const int64_t row_column_cnt = data_store_desc.get_row_column_count();
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_FAIL(shadow_row_.init(row_column_cnt))) {
  } else if (OB_FAIL(row_queue_.init(row_column_cnt))) {
  } else {
    data_store_desc_ = &data_store_desc;
    macro_writer_ = &macro_writer;
    is_inited_ = true;
  }
  return ret;
}

int ObMdsMergeMultiVersionRowStore::finish()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_UNLIKELY(row_queue_.is_empty())) {
    ret = OB_EMPTY_RESULT;
    LOG_WARN("unexpected row queue is empty, which means no data come in", K(ret));
  } else if (OB_FAIL(dump_row_queue())) {
  } else {
  }
  return ret;
}

int ObMdsMergeMultiVersionRowStore::put_row_into_queue(const ObDatumRow &row)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (row_queue_.is_empty()) {
    if (OB_FAIL(row_queue_.add_row(row, row_queue_allocator_))) {
    }
  } else {
    cur_key_.reset();
    last_key_.reset();
    int32_t compare_result = 0;
    const ObDatumRow *last_row_in_qu = row_queue_.get_last();
    if (OB_ISNULL(last_row_in_qu)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected last row is nullptr", K(ret), K(row_queue_));
    } else if (OB_FAIL(last_key_.assign(last_row_in_qu->storage_datums_, data_store_desc_->get_schema_rowkey_col_cnt()))) {
    } else if (OB_FAIL(cur_key_.assign(row.storage_datums_, data_store_desc_->get_schema_rowkey_col_cnt()))) {
    } else if (OB_FAIL(cur_key_.compare(last_key_, data_store_desc_->get_datum_utils(), compare_result))) {
    } else if (OB_UNLIKELY(compare_result < 0)) {
      ret = OB_ROWKEY_ORDER_ERROR;
      LOG_ERROR("input rowkey is less then last rowkey", K(ret), K(cur_key_), K(last_key_), K(ret));
    } else if (compare_result == 0) {
      if (OB_FAIL(put_same_rowkey_row_into_queue(row, *last_row_in_qu))) {
      }
    } else {
      // put another row key, dump current row queue
      if (OB_FAIL(dump_row_queue())) {
      } else if (OB_FAIL(row_queue_.add_row(row, row_queue_allocator_))) {
      }
    }
  }

  return ret;
}

int ObMdsMergeMultiVersionRowStore::put_same_rowkey_row_into_queue(const ObDatumRow &row, const ObDatumRow &last_row_in_qu)
{
  int ret = OB_SUCCESS;
  const int64_t qu_trans = last_row_in_qu.storage_datums_[ObMdsSchemaHelper::SNAPSHOT_IDX].get_int();
  const int64_t qu_sql_no = last_row_in_qu.storage_datums_[ObMdsSchemaHelper::SEQ_NO_IDX].get_int();
  const int64_t cur_trans = row.storage_datums_[ObMdsSchemaHelper::SNAPSHOT_IDX].get_int();
  const int64_t cur_sql_no = row.storage_datums_[ObMdsSchemaHelper::SEQ_NO_IDX].get_int();
  if (qu_trans > cur_trans) {
    ret = OB_ROWKEY_ORDER_ERROR;
    LOG_ERROR("unexpected to check order", K(ret), K(cur_trans), K(qu_trans), K(row), K(last_row_in_qu));
  } else if (qu_trans == cur_trans) {
    if (OB_UNLIKELY(qu_sql_no >= cur_sql_no)) {
      ret = OB_ROWKEY_ORDER_ERROR;
      LOG_ERROR("unexpected to check order", K(ret), K(cur_sql_no), K(qu_sql_no), K(row), K(last_row_in_qu));
    } else {
      // do no thing, mds row is compact row, only need to store smaller sql no (not fresh).
    }
  } else {
    // another trans version rowkey.
    if (OB_FAIL(row_queue_.add_row(row, row_queue_allocator_))) {
    }
  }
  return ret;
}

int ObMdsMergeMultiVersionRowStore::dump_row_queue()
{
  int ret = OB_SUCCESS;
  if (row_queue_.is_empty()) {
    //do nothing
  } else if (1 == row_queue_.count()) {
    ObDatumRow * last_row_in_qu = row_queue_.get_last();
    if (OB_ISNULL(last_row_in_qu)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected last row is nullptr", K(ret), K(row_queue_));
    } else {
      last_row_in_qu->set_first_multi_version_row();
      last_row_in_qu->set_last_multi_version_row();
      last_row_in_qu->set_compacted_multi_version_row();
      last_row_in_qu->storage_datums_[ObMdsSchemaHelper::SEQ_NO_IDX].set_int(0);
      if (OB_FAIL(macro_writer_->append_row(*last_row_in_qu))) {
      } else {
      }
    }
  } else {
    if (OB_FAIL(dump_shadow_row())){
    } else {
      const ObDatumRow *row = nullptr;
      ObDatumRow *dump_row = nullptr;
      while (OB_SUCC(ret) && row_queue_.has_next()) {
        if (OB_FAIL(row_queue_.get_next_row(row))) {
        } else {
          dump_row = const_cast<ObDatumRow *> (row);
          dump_row->storage_datums_[ObMdsSchemaHelper::SEQ_NO_IDX].set_int(0);
          dump_row->set_compacted_multi_version_row();
          if (!row_queue_.has_next()) {
            dump_row->set_last_multi_version_row();
          }
          if (OB_FAIL(macro_writer_->append_row(*dump_row))) {
          } else {
          }
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    row_queue_.print_rows();
  } else {
    row_queue_.reuse();
    row_queue_allocator_.reuse();
  }
  return ret;
}

int ObMdsMergeMultiVersionRowStore::dump_shadow_row()
{
  int ret = OB_SUCCESS;
  shadow_row_.reuse();
  ObDatumRow * first_row_in_qu = row_queue_.get_first();
  if (OB_ISNULL(first_row_in_qu)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected last row is nullptr", K(ret), K(row_queue_));
  } else if (OB_FAIL(shadow_row_.deep_copy((*first_row_in_qu), row_queue_allocator_))) {
  } else {
    shadow_row_.set_first_multi_version_row();
    shadow_row_.set_shadow_row();
    shadow_row_.set_compacted_multi_version_row();
    shadow_row_.storage_datums_[ObMdsSchemaHelper::SEQ_NO_IDX].set_int(-INT64_MAX);
    if (OB_FAIL(macro_writer_->append_row(shadow_row_))) {
    } else {
    }
  }
  return ret;
}

ObMdsMiniMergeOperator::ObMdsMiniMergeOperator()
  : is_inited_(false),
    row_store_(),
    cur_allocator_(common::ObMemAttr("MdsMiniOP")),
    cur_row_()
{
}

int ObMdsMiniMergeOperator::init(
    const ObDataStoreDesc &data_store_desc,
    blocksstable::ObMacroBlockWriter &macro_writer)
{
  int ret = OB_SUCCESS;
  const int64_t row_column_cnt = data_store_desc.get_row_column_count();

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!data_store_desc.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid data store desc", K(ret), K(data_store_desc));
  } else if (OB_FAIL(row_store_.init(data_store_desc, macro_writer))) {
  } else if (OB_FAIL(cur_row_.init(row_column_cnt))) {
  } else {
    is_inited_ = true;
  }

  return ret;
}

int ObTabletDumpMds2MiniOperator::operator()(const mds::MdsDumpKV &kv)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!kv.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dump kv is invalid", K(ret), K(kv));
  } else {
    cur_row_.reuse();
    cur_allocator_.reuse();
    mds::MdsDumpKVStorageAdapter adapter(kv);
    if (OB_FAIL(adapter.convert_to_mds_row(cur_allocator_, cur_row_))) {
    } else if (OB_FAIL(row_store_.put_row_into_queue(cur_row_))) {
    } else {
      LOG_INFO("mds op succeed to add row", K(ret), K(adapter), K(cur_row_), K(kv));
    }
  }

  return ret;
}

int ObTabletDumpMediumMds2MiniOperator::operator()(const mds::MdsDumpKV &kv)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!kv.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dump kv is invalid", K(ret), K(kv));
  } else {
    cur_row_.reuse();
    cur_allocator_.reuse();
    mds::MdsDumpKVStorageAdapter adapter(kv);
    if (OB_FAIL(adapter.convert_to_mds_row(cur_allocator_, cur_row_))) {
    } else if (OB_FAIL(row_store_.put_row_into_queue(cur_row_))) {
    } else {
      LOG_INFO("mds op succeed to add medium mds row", K(ret), K(adapter), K(cur_row_));
    }
  }
  return ret;
}


/*
------------------------------------------ObMdsTableMiniMerger-----------------------------------
*/
ObMdsTableMiniMerger::ObMdsTableMiniMerger()
  : allocator_(common::ObMemAttr("MdsMiniMerger")),
    data_desc_(),
    macro_writer_(),
    sstable_builder_(false/*not use double buffer*/),
    ctx_(nullptr),
    storage_schema_(nullptr),
    is_inited_(false)
{
}

void ObMdsTableMiniMerger::reset()
{
  allocator_.reset();
  data_desc_.reset();
  macro_writer_.reset();
  sstable_builder_.reset();
  ctx_ = nullptr;
  storage_schema_ = nullptr;
  is_inited_ = false;
}

int ObMdsTableMiniMerger::init(compaction::ObTabletMergeCtx &ctx, ObMdsMiniMergeOperator &op)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    MDS_LOG(WARN, "init twice", K(ret));
  } else {
    const common::ObTabletID &tablet_id = ctx.get_tablet_id();
    const ObStorageSchema *storage_schema = ObMdsSchemaHelper::get_instance().get_storage_schema();
    const uint64_t data_version = DATA_CURRENT_VERSION;
    ObMacroDataSeq macro_start_seq(0);
    ObMacroSeqParam macro_seq_param;
    macro_seq_param.seq_type_ = ObMacroSeqParam::SEQ_TYPE_INC;
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(storage_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("storage schema is null", K(ret), KP(storage_schema));
    } else if (OB_UNLIKELY(!storage_schema->is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("mds storage schema is invalid", K(ret), KP(storage_schema), KPC(storage_schema));
    } else if (OB_FAIL(data_desc_.init(false/*is ddl*/, *storage_schema, tablet_id,
        ctx.get_merge_type(), ctx.get_snapshot(), data_version, ctx.static_desc_.micro_index_clustered_,
        ctx.get_concurrent_cnt(), ctx.static_param_.scn_range_.end_scn_))) {
    } else if (OB_FAIL(macro_start_seq.set_parallel_degree(0))) {
    } else if (OB_FAIL(macro_start_seq.set_sstable_seq(ctx.static_param_.sstable_logic_seq_))) {
    } else if (FALSE_IT(macro_seq_param.start_ = macro_start_seq.macro_data_seq_)) {
    } else if (FALSE_IT(data_desc_.get_desc().sstable_index_builder_ = &sstable_builder_)) {
    } else if (OB_FAIL(sstable_builder_.init(data_desc_.get_desc()))) {
    } else if (OB_FAIL(macro_writer_.open(
                   data_desc_.get_desc(), 0 /*parallel_idx*/, macro_seq_param,
                   ctx.get_pre_warm_param()))) {
    } else if (OB_FAIL(op.init(data_desc_.get_desc(), macro_writer_))) {
    } else {
      ctx_ = &ctx;
      storage_schema_ = storage_schema;
      is_inited_ = true;
    }
  }

  return ret;
}

int ObMdsTableMiniMerger::generate_mds_mini_sstable(
    common::ObArenaAllocator &allocator,
    ObTableHandleV2 &table_handle)
{
  int ret = OB_SUCCESS;
  TIMEGUARD_INIT(STORAGE, 20_ms);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret), K_(is_inited));
  } else {
    SMART_VARS_2((ObSSTableMergeRes, res), (ObTabletCreateSSTableParam, param)) {
      if (OB_FAIL(macro_writer_.close())) {
      } else if (OB_FAIL(ctx_->update_block_info(macro_writer_.get_merge_block_info(), 0/*cost_time*/))) {
      } else if (OB_FAIL(sstable_builder_.close(res))) {
      } else if (CLICK_FAIL(param.init_for_mds(*ctx_, res, *storage_schema_))) {
        LOG_ERROR("fail to create sstable param for mds", K(ret));
      } else if (CLICK_FAIL(ObTabletCreateDeleteHelper::create_sstable(param, allocator, table_handle))) {
        LOG_ERROR("fail to create sstable", K(ret), K(param));
        CTX_SET_DIAGNOSE_LOCATION(*ctx_);
      } else {
        // need macro block count for try schedule mds minor after mds mini
        ctx_->get_merge_info().get_merge_history().block_info_.macro_block_count_ = res.data_blocks_cnt_;
      }
    }
  }
  if (OB_FAIL(ret)) {
    FLOG_WARN("fail to generate mds mini sstable", K(ret));
  } else {
    const common::ObTabletID &tablet_id = ctx_->get_tablet_id();
    const blocksstable::ObSSTable *sstable = static_cast<blocksstable::ObSSTable*>(table_handle.get_table());
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
