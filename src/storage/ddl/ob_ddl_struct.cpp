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

#define USING_LOG_PREFIX STORAGE

#include "ob_ddl_struct.h"
#include "query/vector/ob_vector_index_util.h"
#include "storage/ddl/ob_ddl_storage_util.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ddl/ob_tablet_ddl_kv.h"
#include "storage/ob_i_table.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "storage/ddl/ob_direct_insert_sstable_ctx.h"

using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::share;

ObDDLMacroHandle::ObDDLMacroHandle()
  : block_id_()
{

}

ObDDLMacroHandle::ObDDLMacroHandle(const ObDDLMacroHandle &other)
{
  *this = other;
}

ObDDLMacroHandle &ObDDLMacroHandle::operator=(const ObDDLMacroHandle &other)
{
  if (&other != this) {
    (void)set_block_id(other.get_block_id());
  }
  return *this;
}

ObDDLMacroHandle::~ObDDLMacroHandle()
{
  reset_macro_block_ref();
}

int ObDDLMacroHandle::set_block_id(const blocksstable::MacroBlockId &block_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!block_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_FAIL(reset_macro_block_ref())) {
  } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.inc_ref(block_id))) {
  } else {
    block_id_ = block_id;
  }
  return ret;
}

int ObDDLMacroHandle::reset_macro_block_ref()
{
  int ret = OB_SUCCESS;
  if (block_id_.is_valid()) {
    if (OB_FAIL(OB_STORAGE_OBJECT_MGR.dec_ref(block_id_))) {
    } else {
      block_id_.reset();
    }
  }
  return ret;
}

ObDDLMacroBlock::ObDDLMacroBlock()
  : block_handle_(),
    logic_id_(),
    block_type_(DDL_MB_INVALID_TYPE),
    ddl_start_scn_(SCN::min_scn()),
    scn_(SCN::min_scn()),
    table_key_(),
    data_macro_meta_(nullptr),
    buf_(nullptr),
    size_(0),
    merge_slice_idx_(0)
{
}

ObDDLMacroBlock::~ObDDLMacroBlock()
{
}

int ObDDLMacroBlock::set_data_macro_meta(const MacroBlockId &macro_id, const char* macro_block_buf, const int64_t size, const ObDDLMacroBlockType &block_type,
                                         const bool force_set_macro_meta)
{
  int ret = OB_SUCCESS;
  if (!macro_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(macro_id));
  } else if (nullptr == macro_block_buf || 0 >= size) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(macro_block_buf), K(size));
  } else {
    if (OB_FAIL(ObIndexBlockRebuilder::get_macro_meta(macro_block_buf, size, macro_id, allocator_, data_macro_meta_))) {
    }
  }
  return ret;
}

bool ObDDLMacroBlock::is_valid() const
{
  bool ret =  block_handle_.get_block_id().is_valid()
              && DDL_MB_INVALID_TYPE != block_type_
              && ddl_start_scn_.is_valid_and_not_min()
              && scn_.is_valid_and_not_min();
  ret = ret && logic_id_.is_valid() && nullptr != data_macro_meta_ && data_macro_meta_->is_valid();
  return ret;
}

ObDDLKVHandle &ObDDLKVHandle::operator =(const ObDDLKVHandle &other)
{
  if (this != &other) {
    reset();
    if (OB_NOT_NULL(other.ddl_kv_)) {
      ddl_kv_ = other.ddl_kv_;
      ddl_kv_->inc_ref();
    }
  }
  return *this;
}

DEF_TO_STRING(ObDDLKVHandle)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(KPC_(ddl_kv));
  J_OBJ_END();
  return pos;
}

bool ObDDLKVHandle::is_valid() const
{
  return nullptr != ddl_kv_;
}

int ObDDLKVHandle::set_obj(ObDDLKV *ddl_kv)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ddl_kv)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(ddl_kv));
  } else {
    ddl_kv->inc_ref();
    reset();
    ddl_kv_ = ddl_kv;
  }
  return ret;
}

void ObDDLKVHandle::reset()
{
  if (nullptr != ddl_kv_) {
    if (OB_UNLIKELY(!is_valid())) {
      LOG_ERROR_RET(OB_INVALID_ERROR, "invalid ddl kv handle", KP_(ddl_kv));
      ob_abort();
    } else {
      const int64_t ref_cnt = ddl_kv_->dec_ref();
      if (0 == ref_cnt) {
        ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>()->release_ddl_kv(ddl_kv_);
      } else if (OB_UNLIKELY(ref_cnt < 0)) {
        LOG_ERROR_RET(OB_ERR_UNEXPECTED, "table ref cnt may be leaked", K(ref_cnt), KP(ddl_kv_));
      }
    }
  }
  ddl_kv_ = nullptr;
}

ObDDLKVPendingGuard::ObDDLKVPendingGuard(
    ObTablet *tablet,
    const SCN &scn,
    const SCN &start_scn,
    const int64_t snapshot_version,
    const uint64_t data_format_version,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle,
    const ObDirectLoadType direct_load_type)
  : tablet_(tablet), scn_(scn), kv_handle_(), ret_(OB_SUCCESS), can_freeze_(false)
{
  int ret = OB_SUCCESS;
  ObDDLKV *curr_kv = nullptr;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  if (OB_UNLIKELY(nullptr == tablet
      || !scn.is_valid_and_not_min()
      || !start_scn.is_valid_and_not_min()
      || snapshot_version <= 0
      || data_format_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(tablet), K(scn), K(start_scn), K(snapshot_version), K(data_format_version));
  } else if (OB_UNLIKELY(!is_full_direct_load(direct_load_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only support DDL direct load type", KR(ret), K(direct_load_type));
  } else if (ObDDLUtil::use_idempotent_mode()) {
    if (OB_FAIL(tablet->get_ddl_kv_mgr(ddl_kv_mgr_handle, true/*try_create*/))) {
    } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->get_or_create_idem_ddl_kv(
        scn, start_scn, snapshot_version, data_format_version, kv_handle_))) {
    }
  } else {
    if (OB_FAIL(tablet->get_ddl_kv_mgr(ddl_kv_mgr_handle, true /*try_create*/))) {
    } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->get_or_create_local_ddl_kv(
        scn, start_scn, direct_load_mgr_handle, kv_handle_))) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(curr_kv = kv_handle_.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, active ddl kv must not be nullptr", K(ret));
  } else {
    curr_kv->inc_pending_cnt();
    can_freeze_ = ddl_kv_mgr_handle.get_obj()->can_freeze();
  }
  if (OB_FAIL(ret)) {
    kv_handle_.reset();
    ret_ = ret;
  }
}

int ObDDLKVPendingGuard::get_ddl_kv(ObDDLKV *&kv)
{
  int ret = OB_SUCCESS;
  kv = nullptr;
  if (OB_FAIL(ret_)) {
    // do nothing
  } else {
    kv = kv_handle_.get_obj();
  }
  return ret;
}

ObDDLKVPendingGuard::~ObDDLKVPendingGuard()
{
  int ret = OB_SUCCESS;
  if (OB_SUCCESS == ret_) {
    ObDDLKV *curr_kv = kv_handle_.get_obj();
    if (nullptr != curr_kv) {
      curr_kv->dec_pending_cnt();
    }
  }
  kv_handle_.reset();
  can_freeze_ = false;
}

int ObDDLKVPendingGuard::set_macro_block(
    ObTablet *tablet,
    const ObDDLMacroBlock &macro_block,
    const int64_t snapshot_version,
    const uint64_t data_format_version,
    ObTabletDirectLoadMgrHandle &direct_load_mgr_handle,
    const ObDirectLoadType direct_load_type)
{
  int ret = OB_SUCCESS;
  static const int64_t MAX_RETRY_COUNT = 10;
  if (OB_UNLIKELY(nullptr == tablet || !macro_block.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(tablet), K(macro_block));
  } else if (OB_UNLIKELY(!is_full_direct_load(direct_load_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only support DDL direct load type", KR(ret), K(direct_load_type));
  } else {
    int64_t try_count = 0;
    while ((OB_SUCCESS == ret || OB_EAGAIN == ret) && try_count < MAX_RETRY_COUNT) {
      ObDDLKV *ddl_kv = nullptr;
      ObDDLKVPendingGuard guard(tablet, macro_block.scn_, macro_block.ddl_start_scn_,
          snapshot_version, data_format_version, direct_load_mgr_handle,
          direct_load_type);
      if (OB_FAIL(guard.get_ddl_kv(ddl_kv))) {
      } else if (OB_ISNULL(ddl_kv)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl kv is null", K(ret), KP(ddl_kv), K(guard));
      } else if (OB_FAIL(ddl_kv->set_macro_block(*tablet, macro_block, snapshot_version, data_format_version, guard.can_freeze()))) {
      } else {
        break;
      }
      if (OB_EAGAIN == ret) {
        ++try_count;
        LOG_WARN("retry get ddl kv and set macro block", K(try_count));
      }
    }
  }
  return ret;
}

ObDDLMacroBlockRedoInfo::ObDDLMacroBlockRedoInfo()
  : table_key_(),
    data_buffer_(),
    block_type_(ObDDLMacroBlockType::DDL_MB_INVALID_TYPE),
    start_scn_(SCN::min_scn()),
    data_format_version_(0),
    type_(ObDirectLoadType::DIRECT_LOAD_DDL),
    macro_block_id_(MacroBlockId::mock_valid_macro_id()),
    parallel_cnt_(0),
    merge_slice_idx_(0)
{
}

void ObDDLMacroBlockRedoInfo::reset()
{
  table_key_.reset();
  data_buffer_.reset();
  block_type_ = ObDDLMacroBlockType::DDL_MB_INVALID_TYPE;
  logic_id_.reset();
  start_scn_ = SCN::min_scn();
  data_format_version_ = 0;
  type_ = ObDirectLoadType::DIRECT_LOAD_DDL;
  macro_block_id_ = MacroBlockId::mock_valid_macro_id();
  parallel_cnt_ = 0;
  merge_slice_idx_ = 0;
}

bool ObDDLMacroBlockRedoInfo::is_valid() const
{
  bool ret = table_key_.is_valid() && block_type_ != ObDDLMacroBlockType::DDL_MB_INVALID_TYPE
              && logic_id_.is_valid() && start_scn_.is_valid_and_not_min()
              && data_format_version_ >= 0 && macro_block_id_.is_valid()
              && is_full_direct_load(type_);
  if (ret) {
    ret = ret && !((data_buffer_.ptr() == nullptr || data_buffer_.length() == 0));
  }

  return ret;
}

OB_SERIALIZE_MEMBER(ObDDLMacroBlockRedoInfo,
                    table_key_,
                    data_buffer_,
                    block_type_,
                    logic_id_,
                    start_scn_,
                    data_format_version_,
                    type_,
                    macro_block_id_,
                    parallel_cnt_,
                    merge_slice_idx_);

ObTabletDirectLoadMgrHandle::ObTabletDirectLoadMgrHandle()
  : tablet_mgr_(nullptr)
{ }

ObTabletDirectLoadMgrHandle::~ObTabletDirectLoadMgrHandle()
{
  reset();
}

int ObTabletDirectLoadMgrHandle::set_obj(ObBaseTabletDirectLoadMgr *mgr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret));
  } else {
    mgr->inc_ref();
    reset();
    tablet_mgr_ = mgr;
  }
  return ret;
}

ObBaseTabletDirectLoadMgr* ObTabletDirectLoadMgrHandle::get_base_obj()
{
  return tablet_mgr_;
}

const ObBaseTabletDirectLoadMgr* ObTabletDirectLoadMgrHandle::get_base_obj() const
{
  return tablet_mgr_;
}

ObTabletDirectLoadMgr* ObTabletDirectLoadMgrHandle::get_obj()
{
  ObTabletDirectLoadMgr* res = nullptr;
  if (nullptr != tablet_mgr_ && !is_idem_type(tablet_mgr_->get_direct_load_type())) {
    res = static_cast<ObTabletDirectLoadMgr*>(tablet_mgr_);
  }
  return res;
}

const ObTabletDirectLoadMgr *ObTabletDirectLoadMgrHandle::get_obj() const
{
  ObTabletDirectLoadMgr* res = nullptr;
  if (nullptr != tablet_mgr_ && !is_idem_type(tablet_mgr_->get_direct_load_type())) {
    res = static_cast<ObTabletDirectLoadMgr*>(tablet_mgr_);
  }
  return res;
}

ObTabletFullDirectLoadMgr* ObTabletDirectLoadMgrHandle::get_full_obj() const
{
  ObTabletFullDirectLoadMgr* res = nullptr;
  if (nullptr != tablet_mgr_ && !is_idem_type(tablet_mgr_->get_direct_load_type())) {
    res = static_cast<ObTabletFullDirectLoadMgr*>(tablet_mgr_);
  }
  return res;
}

bool ObTabletDirectLoadMgrHandle::is_valid() const
{
  return nullptr != tablet_mgr_;
}

void ObTabletDirectLoadMgrHandle::reset()
{
  if (nullptr != tablet_mgr_) {
    if (0 == tablet_mgr_->dec_ref()) {
      if (is_idem_type(tablet_mgr_->get_direct_load_type())) {
        tablet_mgr_->~ObBaseTabletDirectLoadMgr();
      } else {
        tablet_mgr_->~ObBaseTabletDirectLoadMgr();
        ::oceanbase::share::server_service<::oceanbase::storage::ObDirectLoadMgr>()->get_allocator().free(tablet_mgr_);
      }
    }
    tablet_mgr_ = nullptr;
  }
}

int ObTabletDirectLoadMgrHandle::assign(const ObTabletDirectLoadMgrHandle &other)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_LIKELY(other.is_valid())) {
    if (OB_FAIL(set_obj(other.tablet_mgr_))) {
    }
  }
  return ret;
}

ObDDLWriteStat::ObDDLWriteStat() : row_count_(0)
{ }

ObDDLWriteStat::~ObDDLWriteStat()
{ }

void ObDDLWriteStat::reset()
{
  row_count_ = 0;
}

bool ObDDLWriteStat::is_valid() const
{
  return row_count_ >= 0;
}

int ObDDLWriteStat::assign(const ObDDLWriteStat &other)
{
  int ret  = OB_SUCCESS;
  row_count_ = other.row_count_;
  return ret;
}

bool ObDDLWriteStat::operator!=(const ObDDLWriteStat &other)
{
  return row_count_ != other.row_count_;
}
OB_SERIALIZE_MEMBER(ObDDLWriteStat, row_count_);

int ObDDLTableSchema::fill_vector_index_schema_item(ObSchemaGetterGuard &schema_guard,
    const ObTableSchema *table_schema,
    ObArenaAllocator &allocator,
    const ObIArray<ObColDesc> &column_descs,
    ObDDLTableSchema &ddl_table_schema)
{
  int ret = OB_SUCCESS;
  ObSEArray<uint64_t , 1> col_ids;
  uint64_t with_param_table_tid;
  // for hnsw, table_schema here is snapshot table, need to get related delta buffer table.
  ObIndexType index_type = INDEX_TYPE_VEC_DELTA_BUFFER_LOCAL;

  ObTableSchemaItem &schema_item = ddl_table_schema.table_item_;
  const ObTableSchema *data_table_schema = nullptr;

  // ivf param is saved in centroid table's schema
  if (table_schema->is_vec_ivfflat_index()) {
    index_type = INDEX_TYPE_VEC_IVFFLAT_CENTROID_LOCAL;
  } else if (table_schema->is_vec_ivfsq8_index()) {
    index_type = INDEX_TYPE_VEC_IVFSQ8_CENTROID_LOCAL;
  } else if (table_schema->is_vec_ivfpq_index()) {
    index_type = INDEX_TYPE_VEC_IVFPQ_CENTROID_LOCAL;
  }
  const ObTableSchema *with_param_table_schema = nullptr;
  // get data schema
  if (OB_FAIL(schema_guard.get_table_schema( table_schema->get_data_table_id(), data_table_schema))) {
  } else if (OB_ISNULL(data_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist", K(ret), K(table_schema->get_data_table_id()));
  } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_column_id(*data_table_schema, *table_schema, col_ids))) {
  } else if (col_ids.count() != 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid col id array", K(ret), K(col_ids));
  } else {
    if (index_type == INDEX_TYPE_VEC_DELTA_BUFFER_LOCAL) {
      ObString index_prefix;
      if (OB_FAIL(ObVectorIndexUtil::get_vector_index_prefix(*table_schema, index_prefix))) {
      } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_tid_with_index_prefix(&schema_guard,
                                                                                   *data_table_schema,
                                                                                   index_type,
                                                                                   col_ids.at(0),
                                                                                   index_prefix,
                                                                                   with_param_table_tid))) {
      }
    } else { // ivf centroid tables
      if (OB_FAIL(ObVectorIndexUtil::get_vector_index_tid(&schema_guard,
                                                          *data_table_schema,
                                                          index_type,
                                                          col_ids.at(0),
                                                          with_param_table_tid))) {
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(schema_guard.get_table_schema( with_param_table_tid, with_param_table_schema))) {
  } else if (OB_ISNULL(with_param_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist", K(ret), K(with_param_table_tid));
  } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_column_dim(*with_param_table_schema, *data_table_schema, schema_item.vec_dim_))) {
  } else if (schema_item.vec_dim_ == 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get vector dim is zero, fail to calc", K(ret), K(schema_item.vec_dim_), KPC(with_param_table_schema));
  } else if (OB_FAIL(ob_write_string(allocator, with_param_table_schema->get_index_params(), schema_item.vec_idx_param_))) {
  } else {
    schema_item.lob_inrow_threshold_ = data_table_schema->get_lob_inrow_threshold();
    ObIArray<ObColumnSchemaItem> &column_items = ddl_table_schema.column_items_;
    for (int64_t i = 0; OB_SUCC(ret) && i < column_items.count(); ++i) {
       const schema::ObColumnSchemaV2 *data_column_schema = nullptr;
       ObColumnSchemaItem &column_item = column_items.at(i);
       if (i >= table_schema->get_rowkey_column_num() && i < table_schema->get_rowkey_column_num() + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt()) {
         // skip multi version column, keep item invalid
       } else if (i >= column_descs.count()) {
         ret = OB_ERR_UNEXPECTED;
         LOG_WARN("error unexpected, index is invalid", K(ret), K(i), K(column_descs));
       } else if (OB_ISNULL(data_column_schema = data_table_schema->get_column_schema(column_descs.at(i).col_id_))) {
         ret = OB_ERR_UNEXPECTED;
         LOG_WARN("data column schema is null", K(ret), K(i), K(column_descs.at(i).col_id_));
       } else {
         column_item.column_flags_ = data_column_schema->get_column_flags();
       }
    }
  }
  return ret;
}

int ObDDLTableSchema::fill_ddl_table_schema(const uint64_t table_id,
    ObArenaAllocator &allocator,
    ObDDLTableSchema &ddl_table_schema)
{
  int ret = OB_SUCCESS;
  ObArray<ObColDesc> column_descs;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;
  bool is_vector_data_complement = false;
  if (OB_UNLIKELY(OB_INVALID_ID == table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(table_id));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist", K(ret), K(table_id));
  } else if (OB_FAIL(table_schema->get_multi_version_column_descs(column_descs))) {
  } else {
    ddl_table_schema.table_id_ = table_id;
    ddl_table_schema.table_item_.is_index_table_ = table_schema->is_index_table();
    ddl_table_schema.table_item_.is_unique_index_ = table_schema->is_unique_index();
    ddl_table_schema.table_item_.rowkey_column_num_ = table_schema->get_rowkey_column_num();
    ddl_table_schema.table_item_.lob_inrow_threshold_ = table_schema->get_lob_inrow_threshold();
    ddl_table_schema.table_item_.compress_type_ = table_schema->get_compressor_type();
    ddl_table_schema.table_item_.index_type_ = table_schema->get_index_type();

    if (OB_FAIL(ddl_table_schema.column_descs_.assign(column_descs))) {
    } else if (OB_FAIL(ObDDLStorageUtil::convert_to_storage_schema(table_schema, allocator, ddl_table_schema.storage_schema_))) {
    } else if (OB_INVALID_ID != table_schema->get_aux_lob_meta_tid()) {
      const uint64_t lob_meta_table_id = table_schema->get_aux_lob_meta_tid();
      const ObTableSchema *lob_meta_table_schema = nullptr;
      if (OB_FAIL(schema_guard.get_table_schema( lob_meta_table_id, lob_meta_table_schema))) {
      } else if (OB_ISNULL(lob_meta_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table not exist", K(ret), K(lob_meta_table_id));
      } else if (OB_FAIL(ObDDLStorageUtil::convert_to_storage_schema(lob_meta_table_schema, allocator, ddl_table_schema.lob_meta_storage_schema_))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ddl_table_schema.column_items_.reserve(column_descs.count()))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < column_descs.count(); ++i) {
      const ObColDesc &col_desc = column_descs.at(i);
      const schema::ObColumnSchemaV2 *column_schema = nullptr;
      ObColumnSchemaItem column_item;
      if (i >= ddl_table_schema.table_item_.rowkey_column_num_
          && i < ddl_table_schema.table_item_.rowkey_column_num_ + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt()) {
        column_item.col_type_ = col_desc.col_type_; // for append_batch, skip multi version column, keep item invalid
      } else if (OB_ISNULL(column_schema = table_schema->get_column_schema(col_desc.col_id_))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("column schema is null", K(ret), K(i), K(column_descs), K(col_desc.col_id_));
      } else {
        column_item.is_valid_ = true;
        column_item.col_type_ = column_schema->get_meta_type();
        if (column_schema->is_decimal_int()) {
          column_item.col_type_.set_stored_precision(column_schema->get_accuracy().get_precision());
        }
        column_item.col_accuracy_ = column_schema->get_accuracy();
        column_item.column_flags_ = column_schema->get_column_flags();
        column_item.is_rowkey_column_ = i < ddl_table_schema.table_item_.rowkey_column_num_;
        column_item.is_nullable_ = column_schema->is_nullable();
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(ddl_table_schema.column_items_.push_back(column_item))) {
        } else if (column_item.col_type_.is_lob_storage()) {
          if (OB_FAIL(ddl_table_schema.lob_column_idxs_.push_back(i))) {
          } else if (i < ddl_table_schema.table_item_.rowkey_column_num_) {
            ddl_table_schema.table_item_.has_lob_rowkey_ = true;
          }
        } else if (ObDDLUtil::need_reshape(column_item.col_type_)) {
          if (OB_FAIL(ddl_table_schema.reshape_column_idxs_.push_back(i))) {
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (FALSE_IT(is_vector_data_complement = ObDDLUtil::is_vector_index_complement(table_schema->get_index_type()))) {
      } else if (is_vector_data_complement && OB_FAIL(fill_vector_index_schema_item(schema_guard,
          table_schema,
          allocator,
          column_descs,
          ddl_table_schema))) {
        LOG_WARN("fail to prepare vector index data", K(ret));
      }
    }
  }
  return ret;
}

void ObDDLTableSchema::reset()
{
  table_id_ = 0;
  table_item_.reset();
  storage_schema_ = nullptr;
  lob_meta_storage_schema_ = nullptr;
  column_items_.reset();
  reshape_column_idxs_.reset();
  lob_column_idxs_.reset();
  column_descs_.reset();
}

int ObDDLTableSchema::assign(const ObDDLTableSchema &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(column_items_.assign(other.column_items_))) {
  } else if (OB_FAIL(reshape_column_idxs_.assign(other.reshape_column_idxs_))) {
  } else if (OB_FAIL(lob_column_idxs_.assign(other.lob_column_idxs_))) {
  } else if (OB_FAIL(column_descs_.assign(other.column_descs_))) {
  } else {
    table_id_ = other.table_id_;
    table_item_ = other.table_item_;
    storage_schema_ = other.storage_schema_;
    lob_meta_storage_schema_ = other.lob_meta_storage_schema_;
  }
  return ret;
}
