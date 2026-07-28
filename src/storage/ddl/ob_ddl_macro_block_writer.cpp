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
#include "storage/ddl/ob_ddl_macro_block_writer.h"
#include "storage/ddl/ob_ddl_storage_util.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "storage/ob_storage_schema.h"
#include "storage/ddl/ob_ddl_independent_dag.h"
#include "storage/blocksstable/ob_logic_macro_id.h" // for ObMacroDataSeq
#include "storage/blocksstable/index_block/ob_macro_meta_temp_store.h"
#include "storage/ddl/ob_ddl_write_stat_util.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::sql;
using namespace oceanbase::transaction;

/**
* -----------------------------------ObDDLMacroBlockWriter-----------------------------------
*/
ObDDLMacroBlockWriter::ObDDLMacroBlockWriter()
  : is_inited_(false),
    data_desc_(),
    index_builder_(true/*use_double_write_macro_buffer*/),
    ddl_redo_callback_(),
    macro_block_writer_(true/*use_double_write_macro_buffer*/)
{

}

ObDDLMacroBlockWriter::~ObDDLMacroBlockWriter()
{
  reset();
}

int ObDDLMacroBlockWriter::init(
    const ObWriteMacroParam &param,
    const ObITable::TableKey &table_key,
    const ObMacroDataSeq &start_sequence,
    const int64_t row_offset,
    const int64_t lob_start_seq)
{
  int ret = OB_SUCCESS;
  const ObWriteTabletParam &tablet_param =
      table_key.tablet_id_ != param.tablet_id_ ? param.lob_meta_tablet_param_ : param.tablet_param_;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("initialized twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!param.is_valid()
        || !start_sequence.is_valid()
        || row_offset < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid argument", K(ret), K(param), K(start_sequence), K(row_offset));
  } else if (OB_ISNULL(tablet_param.storage_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("storage schema is null", K(ret), K(table_key), K(param));
  } else if (OB_UNLIKELY(!is_full_direct_load(param.direct_load_type_))) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("only full direct load is supported", KR(ret), K(param.direct_load_type_));
  } else {
    share::SCN mock_start_scn;
    IGNORE_RETURN mock_start_scn.convert_for_tx(SS_DDL_START_SCN_VAL);
    const uint64_t data_format_version = param.data_format_version_;
    const ObDDLMacroBlockType block_type = DDL_MB_DATA_TYPE;
    const bool need_submit_io = true;
    ObMacroSeqParam macro_seq_param;
    ObPreWarmerParam pre_warm_param;
    ObDDLRedoLogWriterCallback *ddl_redo_callback = nullptr;
    blocksstable::ObMacroMetaTempStore *macro_meta_store = nullptr;
    ObDDLWriteStat *ddl_write_stat = nullptr;
    const int64_t parallel_idx = param.slice_idx_;

    macro_seq_param.seq_type_ = ObMacroSeqParam::SEQ_TYPE_INC;
    macro_seq_param.start_ = start_sequence.macro_data_seq_;

    if (OB_FAIL(pre_warm_param.init(table_key.tablet_id_))) {
      LOG_WARN("fail to initialize pre warm param", K(ret), K(table_key.tablet_id_));
    } else if (OB_FAIL(data_desc_.init(true/*is ddl*/,
                                       *tablet_param.storage_schema_,
                                       table_key.get_tablet_id(),
                                       compaction::ObMergeType::MAJOR_MERGE,
                                       table_key.get_snapshot_version(),
                                       data_format_version,
                                       tablet_param.is_micro_index_clustered_,
                                       0/*concurrent_cnt*/,
                                       SCN::min_scn(),
                                       need_submit_io))) {
      LOG_WARN("fail to initialize data store desc", K(ret));
    } else if (OB_FAIL(index_builder_.init(
        data_desc_.get_desc(), ObSSTableIndexBuilder::ENABLE))) {
      LOG_WARN("fail to initialize sstable index builder", K(ret), K(table_key), K(data_desc_));
    } else {
      // for build the tail index block in macro block
      data_desc_.get_desc().sstable_index_builder_ = &index_builder_;
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObDDLStorageWriteUtil::get_ddl_write_stat(param, table_key, ddl_write_stat))) {
      LOG_WARN("get ddl write stat failed", K(ret), K(table_key), K(param), KPC(ddl_write_stat));
    } else if (OB_ISNULL(ddl_redo_callback_ = ddl_redo_callback = OB_NEW(
                           ObDDLRedoLogWriterCallback, ObMemAttr("ddl_redo_cb")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new ObDDLRedoLogWriterCallback", KR(ret));
    } else {
      ObDDLRedoLogWriterCallbackInitParam init_param;
      init_param.tablet_id_ = table_key.tablet_id_;
      init_param.direct_load_type_ = param.direct_load_type_;
      init_param.block_type_ = block_type;
      init_param.table_key_ = table_key;
      init_param.start_scn_ = mock_start_scn;
      init_param.task_id_ = param.task_id_;
      init_param.data_format_version_ = data_format_version;
      init_param.need_delay_ = false;
      init_param.need_submit_io_ = need_submit_io;
      init_param.macro_meta_store_ = macro_meta_store;
      init_param.write_stat_ = ddl_write_stat;
      if (OB_FAIL(ddl_redo_callback->init(init_param))) {
        LOG_WARN("fail to initialize redo log callback", K(ret), K(init_param));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(macro_block_writer_.open(data_desc_.get_desc(),
                                           parallel_idx,
                                           macro_seq_param,
                                           pre_warm_param,
                                           ddl_redo_callback_))) {
        LOG_WARN("fail to open macro block writer",
            K(ret), K(table_key), K(data_desc_), K(start_sequence));
      }
    }
  }
  if (OB_SUCC(ret)) {
    is_inited_ = true;
  }
  return ret;
}

int ObDDLMacroBlockWriter::append_row(const ObDatumRow &curr_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not initialized", K(ret), K(is_inited_));
  } else if (OB_FAIL(macro_block_writer_.append_row(curr_row))) {
    LOG_WARN("write row failed", K(ret), K(curr_row));
  }
  return ret;
}

int ObDDLMacroBlockWriter::append_batch(const blocksstable::ObBatchDatumRows &curr_rows)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not initialized", K(ret), K(is_inited_));
  } else if (OB_FAIL(macro_block_writer_.append_batch(curr_rows))) {
    LOG_WARN("write rows failed", K(ret), K(curr_rows));
  }
  return ret;
}

int ObDDLMacroBlockWriter::close()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not initialized", K(ret), K(is_inited_));
  } else if (OB_FAIL(macro_block_writer_.close())) {
    LOG_WARN("fail to close macro block writer", K(ret));
  }
  return ret;
}

void ObDDLMacroBlockWriter::reset()
{
  is_inited_ = false;
  data_desc_.reset();
  index_builder_.reset();
  OB_DELETE(ObIMacroBlockFlushCallback, ObMemAttr("ddl_redo_cb"), ddl_redo_callback_);
  macro_block_writer_.reset();
}
