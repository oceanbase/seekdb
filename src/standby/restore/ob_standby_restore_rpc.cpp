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
#include "standby/restore/ob_standby_restore_rpc.h"
#include "storage/blocksstable/index_block/ob_index_block_builder.h"
#include "storage/tablet/ob_tablet.h"

namespace oceanbase
{
namespace blocksstable
{
ObMigrationSSTableParam::ObMigrationSSTableParam()
  : allocator_("StbySSTMeta"), basic_meta_(), column_checksums_(), table_key_(), is_small_sstable_(false)
{
  column_checksums_.set_attr(common::ObMemAttr("StbySSTMeta"));
}

ObMigrationSSTableParam::~ObMigrationSSTableParam() { reset(); }

bool ObMigrationSSTableParam::is_valid() const
{
  return basic_meta_.is_valid() && table_key_.is_valid();
}

bool ObMigrationSSTableParam::is_empty_sstable() const
{
  return 0 == basic_meta_.data_macro_block_count_;
}

void ObMigrationSSTableParam::reset()
{
  basic_meta_.reset();
  column_checksums_.reset();
  table_key_.reset();
  is_small_sstable_ = false;
  allocator_.reset();
}

int ObMigrationSSTableParam::assign(const ObMigrationSSTableParam &other)
{
  int ret = OB_SUCCESS;
  reset();
  if (!other.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(column_checksums_.assign(other.column_checksums_))) {
    LOG_WARN("failed to assign standby sstable checksums", K(ret));
  } else {
    basic_meta_ = other.basic_meta_;
    table_key_ = other.table_key_;
    is_small_sstable_ = other.is_small_sstable_;
  }
  return ret;
}

int ObMigrationSSTableParam::build_from_sstable(const ObSSTable &sstable)
{
  int ret = OB_SUCCESS;
  ObSSTableMetaHandle meta_handle;
  reset();
  if (!sstable.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(sstable.get_meta(meta_handle))) {
    LOG_WARN("failed to get source sstable meta", K(ret), K(sstable));
  } else if (OB_FAIL(meta_handle.get_sstable_meta().get_column_checksums(column_checksums_))) {
    LOG_WARN("failed to get source sstable checksums", K(ret), K(sstable));
  } else {
    basic_meta_ = meta_handle.get_sstable_meta().get_basic_meta();
    table_key_ = sstable.get_key();
    is_small_sstable_ = sstable.is_small_sstable();
  }
  return ret;
}

int ObMigrationSSTableParam::check_sstable_meta(const ObSSTableMeta &sstable_meta) const
{
  int ret = OB_SUCCESS;
  if (!is_valid() || !sstable_meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("standby sstable meta is invalid", K(ret), K(*this), K(sstable_meta));
  } else if (OB_FAIL(ObSSTableMetaChecker::check_sstable_basic_meta(
      basic_meta_, sstable_meta.get_basic_meta()))) {
    LOG_WARN("standby sstable basic meta does not match", K(ret), K(*this), K(sstable_meta));
  } else if (column_checksums_.count() != sstable_meta.get_col_checksum_cnt()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("standby sstable column checksum count does not match", K(ret),
        K(column_checksums_.count()), K(sstable_meta.get_col_checksum_cnt()));
  } else {
    const int64_t *local_checksums = sstable_meta.get_col_checksum();
    for (int64_t i = 0; OB_SUCC(ret) && i < column_checksums_.count(); ++i) {
      if (column_checksums_.at(i) != local_checksums[i]) {
        ret = OB_INVALID_DATA;
        LOG_WARN("standby sstable column checksum does not match", K(ret), K(i),
            K(column_checksums_.at(i)), K(local_checksums[i]));
      }
    }
  }
  return ret;
}

int ObMigrationSSTableParam::get_merge_res(ObSSTableMergeRes &res) const
{
  int ret = OB_SUCCESS;
  res.index_blocks_cnt_ = basic_meta_.index_macro_block_count_;
  res.data_blocks_cnt_ = basic_meta_.data_macro_block_count_;
  res.micro_block_cnt_ = basic_meta_.data_micro_block_count_;
  res.data_column_cnt_ = basic_meta_.column_cnt_;
  res.row_count_ = basic_meta_.row_count_;
  res.max_merged_trans_version_ = basic_meta_.max_merged_trans_version_;
  res.contain_uncommitted_row_ = basic_meta_.contain_uncommitted_row_;
  res.occupy_size_ = basic_meta_.occupy_size_;
  res.original_size_ = basic_meta_.original_size_;
  res.data_checksum_ = basic_meta_.data_checksum_;
  res.use_old_macro_block_count_ = basic_meta_.use_old_macro_block_count_;
  res.compressor_type_ = basic_meta_.compressor_type_;
  res.root_row_store_type_ = basic_meta_.root_row_store_type_;
  res.root_macro_seq_ = basic_meta_.root_macro_seq_;
  if (OB_FAIL(res.data_column_checksums_.assign(column_checksums_))) {
    LOG_WARN("failed to assign standby sstable checksums", K(ret));
  }
  return ret;
}

int ObMigrationSSTableParam::serialize(char *buf, const int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(basic_meta_.serialize(buf, len, pos))) {
  } else if (OB_FAIL(column_checksums_.serialize(buf, len, pos))) {
  } else if (OB_FAIL(table_key_.serialize(buf, len, pos))) {
  } else if (OB_FAIL(serialization::encode_bool(buf, len, pos, is_small_sstable_))) {
  }
  return ret;
}

int ObMigrationSSTableParam::deserialize(const char *buf, const int64_t len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_FAIL(basic_meta_.deserialize(buf, len, pos))) {
  } else if (OB_FAIL(column_checksums_.deserialize(buf, len, pos))) {
  } else if (OB_FAIL(table_key_.deserialize(buf, len, pos))) {
  } else if (OB_FAIL(serialization::decode_bool(buf, len, pos, &is_small_sstable_))) {
  }
  return ret;
}

int64_t ObMigrationSSTableParam::get_serialize_size() const
{
  return basic_meta_.get_serialize_size() + column_checksums_.get_serialize_size()
      + table_key_.get_serialize_size() + serialization::encoded_length_bool(is_small_sstable_);
}
} // namespace blocksstable

namespace storage
{
ObMigrationTabletParam::ObMigrationTabletParam()
  : tablet_id_(), is_deleted_(false), tablet_meta_(), storage_schema_(), allocator_("StbyTabMeta")
{
}

ObMigrationTabletParam::~ObMigrationTabletParam() { reset(); }

bool ObMigrationTabletParam::is_valid() const
{
  return tablet_id_.is_valid()
      && (is_deleted_ || (tablet_meta_.is_valid() && storage_schema_.is_valid()));
}

bool ObMigrationTabletParam::is_empty_shell() const
{
  return !is_deleted_ && tablet_meta_.is_empty_shell_;
}

void ObMigrationTabletParam::reset()
{
  tablet_id_.reset();
  is_deleted_ = false;
  tablet_meta_.reset();
  storage_schema_.reset();
  allocator_.reset();
}

int ObMigrationTabletParam::assign(const ObMigrationTabletParam &other)
{
  int ret = OB_SUCCESS;
  reset();
  if (!other.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!other.is_deleted_ && OB_FAIL(tablet_meta_.assign(other.tablet_meta_))) {
    LOG_WARN("failed to assign standby tablet meta", K(ret));
  } else if (!other.is_deleted_ && OB_FAIL(storage_schema_.assign(allocator_, other.storage_schema_))) {
    LOG_WARN("failed to assign standby storage schema", K(ret));
  } else {
    tablet_id_ = other.tablet_id_;
    is_deleted_ = other.is_deleted_;
  }
  return ret;
}

int ObMigrationTabletParam::build_deleted_tablet_info(
    const share::ObLSID &ls_id, const common::ObTabletID &tablet_id)
{
  UNUSED(ls_id);
  int ret = OB_SUCCESS;
  reset();
  if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    tablet_id_ = tablet_id;
    is_deleted_ = true;
  }
  return ret;
}

int ObMigrationTabletParam::build_from_tablet(const ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  ObStorageSchema *schema = nullptr;
  reset();
  if (!tablet.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(tablet_meta_.assign(tablet.get_tablet_meta()))) {
    LOG_WARN("failed to assign source tablet meta", K(ret));
  } else if (OB_FAIL(tablet.load_storage_schema(allocator_, schema))) {
    LOG_WARN("failed to load source storage schema", K(ret));
  } else if (OB_ISNULL(schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(storage_schema_.assign(allocator_, *schema))) {
    LOG_WARN("failed to assign source storage schema", K(ret));
  } else {
    tablet_id_ = tablet_meta_.tablet_id_;
  }
  ObTabletObjLoadHelper::free(allocator_, schema);
  return ret;
}

int ObMigrationTabletParam::serialize(char *buf, const int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tablet_id_.serialize(buf, len, pos))) {
  } else if (OB_FAIL(serialization::encode_bool(buf, len, pos, is_deleted_))) {
  } else if (!is_deleted_ && OB_FAIL(tablet_meta_.serialize(buf, len, pos))) {
  } else if (!is_deleted_ && OB_FAIL(storage_schema_.serialize(buf, len, pos))) {
  }
  return ret;
}

int ObMigrationTabletParam::deserialize(const char *buf, const int64_t len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_FAIL(tablet_id_.deserialize(buf, len, pos))) {
  } else if (OB_FAIL(serialization::decode_bool(buf, len, pos, &is_deleted_))) {
  } else if (!is_deleted_ && OB_FAIL(tablet_meta_.deserialize(buf, len, pos))) {
  } else if (!is_deleted_ && OB_FAIL(storage_schema_.deserialize(allocator_, buf, len, pos))) {
  }
  return ret;
}

int64_t ObMigrationTabletParam::get_serialize_size() const
{
  int64_t len = tablet_id_.get_serialize_size() + serialization::encoded_length_bool(is_deleted_);
  if (!is_deleted_) {
    len += tablet_meta_.get_serialize_size() + storage_schema_.get_serialize_size();
  }
  return len;
}

ObCopyMacroRangeInfo::ObCopyMacroRangeInfo()
  : start_macro_block_id_(), end_macro_block_id_(), macro_block_count_(0),
    is_leader_restore_(false), start_macro_block_end_key_(datums_, OB_INNER_MAX_ROWKEY_COLUMN_NUMBER),
    allocator_("StbyMacroRange")
{
}

bool ObCopyMacroRangeInfo::is_valid() const
{
  return start_macro_block_id_.is_valid() && end_macro_block_id_.is_valid()
      && macro_block_count_ > 0 && start_macro_block_end_key_.is_valid();
}

void ObCopyMacroRangeInfo::reset()
{
  start_macro_block_id_.reset();
  end_macro_block_id_.reset();
  macro_block_count_ = 0;
  is_leader_restore_ = false;
  start_macro_block_end_key_.reset();
  allocator_.reset();
}

void ObCopyMacroRangeInfo::reuse()
{
  reset();
  start_macro_block_end_key_.datums_ = datums_;
  start_macro_block_end_key_.datum_cnt_ = OB_INNER_MAX_ROWKEY_COLUMN_NUMBER;
}

int ObCopyMacroRangeInfo::deep_copy_start_end_key(const blocksstable::ObDatumRowkey &rowkey)
{
  int ret = OB_SUCCESS;
  if (!rowkey.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("deep copy start end key get invalid argument", K(ret), K(rowkey));
  } else if (OB_FAIL(rowkey.deep_copy(start_macro_block_end_key_, allocator_))) {
    LOG_WARN("failed to copy start macro block end key", K(ret), K(rowkey));
  }
  return ret;
}

int ObCopyMacroRangeInfo::assign(const ObCopyMacroRangeInfo &other)
{
  int ret = OB_SUCCESS;
  if (!other.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("copy macro range info is invalid", K(ret), K(other));
  } else if (OB_FAIL(deep_copy_start_end_key(other.start_macro_block_end_key_))) {
    LOG_WARN("failed to deep copy start end key", K(ret), K(other));
  } else {
    start_macro_block_id_ = other.start_macro_block_id_;
    end_macro_block_id_ = other.end_macro_block_id_;
    macro_block_count_ = other.macro_block_count_;
    is_leader_restore_ = other.is_leader_restore_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObCopyMacroRangeInfo, start_macro_block_id_, end_macro_block_id_,
    macro_block_count_, is_leader_restore_, start_macro_block_end_key_);

ObCopySSTableMacroRangeInfo::ObCopySSTableMacroRangeInfo()
  : copy_table_key_(), copy_macro_range_array_()
{
  copy_macro_range_array_.set_attr(common::ObMemAttr("StbyMacroRange"));
}

bool ObCopySSTableMacroRangeInfo::is_valid() const
{
  return copy_table_key_.is_valid();
}

void ObCopySSTableMacroRangeInfo::reset()
{
  copy_table_key_.reset();
  copy_macro_range_array_.reset();
}

int ObCopySSTableMacroRangeInfo::assign(const ObCopySSTableMacroRangeInfo &other)
{
  int ret = OB_SUCCESS;
  if (!other.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("copy sstable macro range info is invalid", K(ret), K(other));
  } else if (OB_FAIL(copy_macro_range_array_.assign(other.copy_macro_range_array_))) {
    LOG_WARN("failed to assign sstable macro range info", K(ret), K(other));
  } else {
    copy_table_key_ = other.copy_table_key_;
  }
  return ret;
}
} // namespace storage

namespace obcall
{
ObCopyMacroBlockInfo::ObCopyMacroBlockInfo() : logical_id_(), data_type_(MAX) {}
OB_SERIALIZE_MEMBER(ObCopyMacroBlockInfo, logical_id_, data_type_);

ObCopyMacroBlockRangeArg::ObCopyMacroBlockRangeArg()
  : ls_id_(), table_key_(), data_version_(0), backfill_tx_scn_(share::SCN::min_scn()),
    copy_macro_range_info_(), copy_macro_block_infos_()
{
}
bool ObCopyMacroBlockRangeArg::is_valid() const
{
  return ls_id_.is_valid() && table_key_.is_valid()
      && data_version_ >= DISABLE_MACRO_BLOCK_REUSE_DATA_VERSION
      && backfill_tx_scn_ >= share::SCN::min_scn() && copy_macro_range_info_.is_valid();
}
OB_SERIALIZE_MEMBER(ObCopyMacroBlockRangeArg, ls_id_, table_key_, data_version_,
    backfill_tx_scn_, copy_macro_range_info_, copy_macro_block_infos_);

ObCopyMacroBlockHeader::ObCopyMacroBlockHeader()
  : is_reuse_macro_block_(false), occupy_size_(0), data_type_(MACRO_DATA) {}
void ObCopyMacroBlockHeader::reset()
{
  is_reuse_macro_block_ = false;
  occupy_size_ = 0;
  data_type_ = MACRO_DATA;
}
OB_SERIALIZE_MEMBER(ObCopyMacroBlockHeader, is_reuse_macro_block_, occupy_size_, data_type_);

ObCopyTabletInfoArg::ObCopyTabletInfoArg()
  : ls_id_(), tablet_id_list_(), version_(OB_INVALID_ID) {}
OB_SERIALIZE_MEMBER(ObCopyTabletInfoArg, ls_id_, tablet_id_list_, version_);

ObCopyTabletInfo::ObCopyTabletInfo()
  : tablet_id_(), status_(storage::ObCopyTabletStatus::MAX_STATUS), param_(),
    data_size_(0), version_(OB_INVALID_ID) {}
void ObCopyTabletInfo::reset()
{
  tablet_id_.reset();
  status_ = storage::ObCopyTabletStatus::MAX_STATUS;
  param_.reset();
  data_size_ = 0;
  version_ = OB_INVALID_ID;
}
bool ObCopyTabletInfo::is_valid() const
{
  return tablet_id_.is_valid() && storage::ObCopyTabletStatus::is_valid(status_)
      && (status_ == storage::ObCopyTabletStatus::TABLET_NOT_EXIST
          || (param_.is_valid() && data_size_ >= 0))
      && version_ != OB_INVALID_ID;
}
OB_SERIALIZE_MEMBER(ObCopyTabletInfo, tablet_id_, status_, param_, data_size_, version_);

ObCopyTabletSSTableInfoArg::ObCopyTabletSSTableInfoArg()
  : tablet_id_(), max_major_sstable_snapshot_(0), minor_sstable_scn_range_(), ddl_sstable_scn_range_() {}
void ObCopyTabletSSTableInfoArg::reset()
{
  tablet_id_.reset();
  max_major_sstable_snapshot_ = 0;
  minor_sstable_scn_range_.reset();
  ddl_sstable_scn_range_.reset();
}
bool ObCopyTabletSSTableInfoArg::is_valid() const
{
  return tablet_id_.is_valid() && max_major_sstable_snapshot_ >= 0
      && minor_sstable_scn_range_.is_valid() && ddl_sstable_scn_range_.is_valid();
}
OB_SERIALIZE_MEMBER(ObCopyTabletSSTableInfoArg, tablet_id_, max_major_sstable_snapshot_,
    minor_sstable_scn_range_, ddl_sstable_scn_range_);

ObCopyTabletsSSTableInfoArg::ObCopyTabletsSSTableInfoArg()
  : ls_id_(), tablet_sstable_info_arg_list_(), version_(OB_INVALID_ID) {}
void ObCopyTabletsSSTableInfoArg::reset()
{
  ls_id_.reset();
  tablet_sstable_info_arg_list_.reset();
  version_ = OB_INVALID_ID;
}
int ObCopyTabletsSSTableInfoArg::assign(const ObCopyTabletsSSTableInfoArg &other)
{
  int ret = tablet_sstable_info_arg_list_.assign(other.tablet_sstable_info_arg_list_);
  if (OB_SUCC(ret)) {
    ls_id_ = other.ls_id_;
    version_ = other.version_;
  }
  return ret;
}
OB_SERIALIZE_MEMBER(ObCopyTabletsSSTableInfoArg, ls_id_, tablet_sstable_info_arg_list_, version_);

ObCopyTabletSSTableInfo::ObCopyTabletSSTableInfo() : tablet_id_(), table_key_(), param_() {}
void ObCopyTabletSSTableInfo::reset()
{
  tablet_id_.reset();
  table_key_.reset();
  param_.reset();
}
int ObCopyTabletSSTableInfo::assign(const ObCopyTabletSSTableInfo &other)
{
  int ret = param_.assign(other.param_);
  if (OB_SUCC(ret)) {
    tablet_id_ = other.tablet_id_;
    table_key_ = other.table_key_;
  }
  return ret;
}
bool ObCopyTabletSSTableInfo::is_valid() const
{
  return tablet_id_.is_valid() && table_key_.is_valid() && param_.is_valid();
}
OB_SERIALIZE_MEMBER(ObCopyTabletSSTableInfo, tablet_id_, table_key_, param_);

ObCheckRestorePreconditionResult::ObCheckRestorePreconditionResult()
  : required_disk_size_(0), total_tablet_size_(0), data_version_(0) {}
OB_SERIALIZE_MEMBER(ObCheckRestorePreconditionResult,
    required_disk_size_, total_tablet_size_, data_version_);

ObCopySSTableMacroRangeInfoArg::ObCopySSTableMacroRangeInfoArg()
  : ls_id_(), tablet_id_(), copy_table_key_array_(), macro_range_max_marco_count_(0) {}
bool ObCopySSTableMacroRangeInfoArg::is_valid() const
{
  return ls_id_.is_valid() && tablet_id_.is_valid()
      && !copy_table_key_array_.empty() && macro_range_max_marco_count_ > 0;
}
int ObCopySSTableMacroRangeInfoArg::assign(const ObCopySSTableMacroRangeInfoArg &other)
{
  int ret = copy_table_key_array_.assign(other.copy_table_key_array_);
  if (OB_SUCC(ret)) {
    ls_id_ = other.ls_id_;
    tablet_id_ = other.tablet_id_;
    macro_range_max_marco_count_ = other.macro_range_max_marco_count_;
  }
  return ret;
}
OB_SERIALIZE_MEMBER(ObCopySSTableMacroRangeInfoArg, ls_id_, tablet_id_,
    copy_table_key_array_, macro_range_max_marco_count_);

ObCopySSTableMacroRangeInfoHeader::ObCopySSTableMacroRangeInfoHeader()
  : copy_table_key_(), macro_range_count_(0) {}
bool ObCopySSTableMacroRangeInfoHeader::is_valid() const
{
  return copy_table_key_.is_valid() && macro_range_count_ >= 0;
}
void ObCopySSTableMacroRangeInfoHeader::reset()
{
  copy_table_key_.reset();
  macro_range_count_ = 0;
}
OB_SERIALIZE_MEMBER(ObCopySSTableMacroRangeInfoHeader, copy_table_key_, macro_range_count_);

ObCopyTabletSSTableHeader::ObCopyTabletSSTableHeader()
  : tablet_id_(), status_(storage::ObCopyTabletStatus::MAX_STATUS), sstable_count_(0),
    tablet_meta_(), version_(OB_INVALID_ID) {}
void ObCopyTabletSSTableHeader::reset()
{
  tablet_id_.reset();
  status_ = storage::ObCopyTabletStatus::MAX_STATUS;
  sstable_count_ = 0;
  tablet_meta_.reset();
  version_ = OB_INVALID_ID;
}
bool ObCopyTabletSSTableHeader::is_valid() const
{
  return tablet_id_.is_valid() && storage::ObCopyTabletStatus::is_valid(status_)
      && sstable_count_ >= 0
      && (status_ == storage::ObCopyTabletStatus::TABLET_NOT_EXIST || tablet_meta_.is_valid())
      && version_ != OB_INVALID_ID;
}
OB_SERIALIZE_MEMBER(ObCopyTabletSSTableHeader,
    tablet_id_, status_, sstable_count_, tablet_meta_, version_);
} // namespace obcall
} // namespace oceanbase
