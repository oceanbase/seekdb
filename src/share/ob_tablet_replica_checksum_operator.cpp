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

#define USING_LOG_PREFIX SHARE

#include "share/ob_tablet_replica_checksum_operator.h"
#include "storage/compaction/ob_tenant_tablet_scheduler.h"
#include "share/storage/ob_tablet_replica_checksum_table_storage.h"
#include "share/storage/ob_sqlite_connection.h"
#include "observer/ob_server_struct.h"
#ifdef OB_BUILD_SHARED_STORAGE
#include "share/compaction/ob_shared_storage_compaction_util.h"
#endif
namespace oceanbase
{
namespace share
{
using namespace oceanbase::common;

// Static storage instance
ObTabletReplicaChecksumTableStorage ObTabletReplicaChecksumOperator::storage_;

ObTabletReplicaReportColumnMeta::ObTabletReplicaReportColumnMeta()
  : compat_version_(0),
    checksum_method_(0),
    checksum_bytes_(0),
    column_checksums_(),
    is_inited_(false)
{}

ObTabletReplicaReportColumnMeta::~ObTabletReplicaReportColumnMeta()
{
  reset();
}

void ObTabletReplicaReportColumnMeta::reset()
{
  is_inited_ = false;
  compat_version_ = 0;
  checksum_method_ = 0;
  checksum_bytes_ = 0;
  column_checksums_.reset();
}

bool ObTabletReplicaReportColumnMeta::is_valid() const
{
  return is_inited_ && column_checksums_.count() > 0;
}

int ObTabletReplicaReportColumnMeta::init(const ObIArray<int64_t> &column_checksums)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTabletReplicaReportColumnMeta inited twice", KR(ret), K(*this));
  } else if (column_checksums.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_FAIL(column_checksums_.assign(column_checksums))) {
    LOG_WARN("fail to assign column_checksums", KR(ret));
  } else {
    checksum_bytes_ = (sizeof(int16_t) + sizeof(int64_t) + sizeof(int8_t)) * 2;
    checksum_method_ = 0; // TODO
    is_inited_ = true;
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::assign(const ObTabletReplicaReportColumnMeta &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    if (other.column_checksums_.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret));
    } else if (OB_FAIL(column_checksums_.assign(other.column_checksums_))) {
      LOG_WARN("fail to assign column_checksums", KR(ret));
    } else {
      compat_version_ = other.compat_version_;
      checksum_method_ = other.checksum_method_;
      checksum_bytes_ = other.checksum_bytes_;
      is_inited_ = true;
    }
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  int64_t serialize_size = get_serialize_size();
  if (OB_UNLIKELY(NULL == buf) || (serialize_size > buf_len)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments.", KP(buf), KR(ret), K(serialize_size), K(buf_len));
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, pos, MAGIC_NUMBER))) {
    LOG_WARN("fail to encode magic number", KR(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, compat_version_))) {
    LOG_WARN("fail to encode compat version", KR(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, checksum_method_))) {
    LOG_WARN("fail to encode checksum method", KR(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, checksum_bytes_))) {
    LOG_WARN("fail to encode checksum bytes", KR(ret));
  } else if (OB_FAIL(column_checksums_.serialize(buf, buf_len, pos))) {
    LOG_WARN("fail to serialize column_checksums", KR(ret));
  }
  return ret;
}

int64_t ObTabletReplicaReportColumnMeta::get_serialize_size() const
{
  int64_t len = 0;
  len += serialization::encoded_length_i64(MAGIC_NUMBER);
  len += serialization::encoded_length_i8(compat_version_);
  len += serialization::encoded_length_i8(checksum_method_);
  len += serialization::encoded_length_i8(checksum_bytes_);
  len += column_checksums_.get_serialize_size();
  return len;
}

int ObTabletReplicaReportColumnMeta::deserialize(const char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t magic_number = 0;
  if (OB_ISNULL(buf) || (buf_len < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", KR(ret), K(buf), K(buf_len));
  } else if (OB_FAIL(serialization::decode_i64(buf, buf_len, pos, &magic_number))) {
    LOG_WARN("fail to encode magic number", KR(ret));
  } else if (OB_FAIL(serialization::decode_i8(buf, buf_len, pos, &compat_version_))) {
    LOG_WARN("fail to deserialize compat version", KR(ret));
  } else if (OB_FAIL(serialization::decode_i8(buf, buf_len, pos, &checksum_method_))) {
    LOG_WARN("fail to deserialize checksum method", KR(ret));
  } else if (OB_FAIL(serialization::decode_i8(buf, buf_len, pos, &checksum_bytes_))) {
    LOG_WARN("fail to deserialize checksum bytes", KR(ret));
  } else if (OB_FAIL(column_checksums_.deserialize(buf, buf_len, pos))) {
    LOG_WARN("fail to deserialize column checksums", KR(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int64_t ObTabletReplicaReportColumnMeta::get_string_length() const
{
  int64_t len = 0;
  len += sizeof("magic:%lX,");
  len += sizeof("compat:%d,");
  len += sizeof("method:%d,");
  len += sizeof("bytes:%d,");
  len += sizeof("colcnt:%d,");
  len += sizeof("%d:%ld,") * column_checksums_.count();
  len += get_serialize_size();
  return len;
}

int64_t ObTabletReplicaReportColumnMeta::get_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  int32_t column_cnt = static_cast<int32_t>(column_checksums_.count());
  common::databuff_printf(buf, buf_len, pos, "magic:%lX,", MAGIC_NUMBER);
  common::databuff_printf(buf, buf_len, pos, "compat:%d,", compat_version_);
  common::databuff_printf(buf, buf_len, pos, "method:%d,", checksum_method_);
  common::databuff_printf(buf, buf_len, pos, "bytes:%d,", checksum_bytes_);
  common::databuff_printf(buf, buf_len, pos, "colcnt:%d,", column_cnt);

  for (int32_t i = 0; i < column_cnt; ++i) {
    if (column_cnt - 1 != i) {
      common::databuff_printf(buf, buf_len, pos, "%d:%ld,", i, column_checksums_.at(i));
    } else {
      common::databuff_printf(buf, buf_len, pos, "%d:%ld", i, column_checksums_.at(i));
    }
  }
  return pos;
}

int ObTabletReplicaReportColumnMeta::check_checksum(
    const ObTabletReplicaReportColumnMeta &other,
    const int64_t pos, bool &is_equal) const
{
  int ret = OB_SUCCESS;
  is_equal = true;
  const int64_t col_ckm_cnt = column_checksums_.count();
  const int64_t other_col_ckm_cnt = other.column_checksums_.count();
  if ((pos < 0) || (pos > col_ckm_cnt) || (pos > other_col_ckm_cnt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", KR(ret), K(pos), K(col_ckm_cnt), K(other_col_ckm_cnt),
      K(column_checksums_), K(other.column_checksums_));
  } else if (column_checksums_.at(pos) != other.column_checksums_.at(pos)) {
    is_equal = false;
    LOG_WARN("column checksum is not equal!", K(pos), "col_ckm", column_checksums_.at(pos),
      "other_col_ckm", other.column_checksums_.at(pos), K(col_ckm_cnt), K(other_col_ckm_cnt),
      K(column_checksums_), K(other.column_checksums_));
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::check_all_checksums(
    const ObTabletReplicaReportColumnMeta &other,
    bool &is_equal) const
{
  int ret = OB_SUCCESS;
  is_equal = true;
  if (column_checksums_.count() != other.column_checksums_.count()) {
    is_equal = false;
    LOG_WARN("column cnt is not equal!", "cur_cnt", column_checksums_.count(),
      "other_cnt", other.column_checksums_.count(), K(*this), K(other));
  } else {
    const int64_t column_ckm_cnt = column_checksums_.count();
    for (int64_t i = 0; OB_SUCC(ret) && is_equal && (i < column_ckm_cnt); ++i) {
      if (OB_FAIL(check_checksum(other, i, is_equal))) {
        LOG_WARN("fail to check checksum", KR(ret), K(i), K(column_ckm_cnt));
      }
    }
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::check_equal(
    const ObTabletReplicaReportColumnMeta &other,
    bool &is_equal) const
{
  int ret = OB_SUCCESS;
  is_equal = true;
  if (compat_version_ != other.compat_version_) {
    is_equal = false;
    LOG_WARN("compat version is not equal !", K(*this), K(other));
  } else if (checksum_method_ != other.checksum_method_) {
    is_equal = false;
    LOG_WARN("checksum method is different !", K(*this), K(other));
  } else if (OB_FAIL(check_all_checksums(other, is_equal))) {
    LOG_WARN("fail to check all checksum", KR(ret), K(*this), K(other));
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::set_with_str(
    const int64_t compaction_scn_val,
    const ObString &str)
{
  int ret = OB_SUCCESS;
  share::ObFreezeInfo freeze_info;
  uint64_t compaction_data_version = 0;
  if (OB_FAIL(MTL(ObTenantFreezeInfoMgr *)->get_lower_bound_freeze_info_before_snapshot_version(compaction_scn_val, freeze_info))) {
    LOG_WARN("failed to get freeze info", K(ret), K(compaction_scn_val));
  } else if (FALSE_IT(compaction_data_version = freeze_info.data_version_)) {
  } else {
    if (OB_FAIL(set_with_serialize_str(str))) {
      LOG_WARN("failed to set column meta with serialize str", K(ret), K(str));
    }
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::set_with_str(
    const ObDataChecksumType type,
    const ObString &str)
{
  int ret = OB_SUCCESS;
  if (!is_valid_data_checksum_type(type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid column checksum type", K(type));
  } else if (is_normal_column_checksum_type(type)) {
    if (OB_FAIL(set_with_serialize_str(str))) {
      LOG_WARN("failed to set column meta with serialize str", K(ret), K(str));
    }
  } else if (OB_FAIL(set_with_hex_str(str))) {
    LOG_WARN("failed to set column meta with hex str", K(ret), K(str));
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::set_with_hex_str(const common::ObString &hex_str)
{
  int ret = OB_SUCCESS;
  const int64_t hex_str_len = hex_str.length();
  if (hex_str_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(hex_str_len), K(hex_str));
  } else {
    const int64_t deserialize_size = ObTabletReplicaReportColumnMeta::MAX_OCCUPIED_BYTES;
    int64_t deserialize_pos = 0;
    char *deserialize_buf = NULL;
    ObArenaAllocator allocator;

    if (OB_ISNULL(deserialize_buf = static_cast<char *>(allocator.alloc(deserialize_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", KR(ret), K(deserialize_size));
    } else if (OB_FAIL(hex_to_cstr(hex_str.ptr(), hex_str_len, deserialize_buf, deserialize_size))) {
      LOG_WARN("fail to get cstr from hex", KR(ret), K(hex_str_len), K(deserialize_size));
    } else if (OB_FAIL(deserialize(deserialize_buf, deserialize_size, deserialize_pos))) {
      LOG_WARN("fail to deserialize from str to build column meta", KR(ret), "column_meta_str", hex_str.ptr());
    } else if (deserialize_pos > deserialize_size) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("deserialize size overflow", KR(ret), K(deserialize_pos), K(deserialize_size));
    }
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::set_with_serialize_str(const common::ObString &serialize_str)
{
  int ret = OB_SUCCESS;
  const int64_t serialize_len = serialize_str.length();
  int64_t pos = 0;
  if (serialize_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(serialize_len), K(serialize_str));
  } else if (OB_FAIL(deserialize(serialize_str.ptr(), serialize_len, pos))) {
    LOG_WARN("fail to deserialize from str to build column meta", KR(ret), "column_meta_str", serialize_str.ptr());
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::get_str_obj(
    const ObDataChecksumType type,
    common::ObIAllocator &allocator,
    ObObj &obj,
    common::ObString &str) const
{
  int ret = OB_SUCCESS;
  if (!is_valid_data_checksum_type(type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(type));
  } else if (is_normal_column_checksum_type(type)) {
    if (OB_FAIL(get_serialize_str(allocator, str))) {
      LOG_WARN("get serialize column meta str failed", K(ret));
    } else {
      obj.set_varbinary(str);
    }
  } else if (OB_FAIL(get_hex_str(allocator, str))) {
    LOG_WARN("get hex column meta failed", K(ret));
  } else {
    obj.set_varchar(str);
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::get_hex_str(
    common::ObIAllocator &allocator,
    common::ObString &column_meta_hex_str) const
{
  int ret = OB_SUCCESS;
  char *serialize_buf = NULL;
  const int64_t serialize_size = get_serialize_size();
  int64_t serialize_pos = 0;
  char *hex_buf = NULL;
  const int64_t hex_size = 2 * serialize_size;
  int64_t hex_pos = 0;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("column_meta is invlaid", KR(ret), K(*this));
  } else if (OB_UNLIKELY(hex_size > OB_MAX_LONGTEXT_LENGTH + 1)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("format str is too long", KR(ret), K(hex_size), K(*this));
  } else if (OB_ISNULL(serialize_buf = static_cast<char *>(allocator.alloc(serialize_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc buf", KR(ret), K(serialize_size));
  } else if (OB_FAIL(serialize(serialize_buf, serialize_size, serialize_pos))) {
    LOG_WARN("failed to serialize column meta", KR(ret), K(*this), K(serialize_size), K(serialize_pos));
  } else if (OB_UNLIKELY(serialize_pos > serialize_size)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("serialize error", KR(ret), K(serialize_pos), K(serialize_size));
  } else if (OB_ISNULL(hex_buf = static_cast<char*>(allocator.alloc(hex_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", KR(ret), K(hex_size));
  } else if (OB_FAIL(hex_print(serialize_buf, serialize_pos, hex_buf, hex_size, hex_pos))) {
    LOG_WARN("fail to print hex", KR(ret), K(serialize_pos), K(hex_size), K(serialize_buf));
  } else if (OB_UNLIKELY(hex_pos > hex_size)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("encode error", KR(ret), K(hex_pos), K(hex_size));
  } else {
    column_meta_hex_str.assign_ptr(hex_buf, static_cast<int32_t>(hex_size));
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::get_serialize_str(
    common::ObIAllocator &allocator,
    common::ObString &str) const
{
  int ret = OB_SUCCESS;
  char *serialize_buf = NULL;
  const int64_t serialize_size = get_serialize_size();
  int64_t serialize_pos = 0;
  int64_t hex_pos = 0;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "column_meta is invlaid", KR(ret), K(*this));
  } else if (OB_UNLIKELY(serialize_size > OB_MAX_VARBINARY_LENGTH)) {
    ret = OB_SIZE_OVERFLOW;
    SHARE_LOG(WARN, "format str is too long", KR(ret), K(*this));
  } else if (OB_ISNULL(serialize_buf = static_cast<char *>(allocator.alloc(serialize_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SHARE_LOG(WARN, "fail to alloc buf", KR(ret), K(serialize_size));
  } else if (OB_FAIL(serialize(serialize_buf, serialize_size, serialize_pos))) {
    LOG_WARN("failed to serialize column meta", KR(ret), K(*this), K(serialize_size), K(serialize_pos));
  } else if (OB_UNLIKELY(serialize_pos > serialize_size)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("serialize error", KR(ret), K(serialize_pos), K(serialize_size));
  } else {
    str.assign_ptr(serialize_buf, static_cast<int32_t>(serialize_size));
  }
  return ret;
}

/****************************** ObTabletReplicaChecksumItem ******************************/

ObTabletReplicaChecksumItem::ObTabletReplicaChecksumItem()
  : tenant_id_(OB_SYS_TENANT_ID),
    ls_id_(),
    tablet_id_(),
    server_(),
    row_count_(0),
    compaction_scn_(),
    data_checksum_(0),
    column_meta_(),
    data_checksum_type_(ObDataChecksumType::DATA_CHECKSUM_MAX),
    co_base_snapshot_version_()
{}

void ObTabletReplicaChecksumItem::reset()
{
  tenant_id_ = OB_SYS_TENANT_ID;
  ls_id_.reset();
  tablet_id_.reset();
  server_.reset();
  row_count_ = 0;
  compaction_scn_.reset();
  data_checksum_ = 0;
  column_meta_.reset();
  data_checksum_type_ = ObDataChecksumType::DATA_CHECKSUM_MAX;
  co_base_snapshot_version_.reset();
}

bool ObTabletReplicaChecksumItem::is_key_valid() const
{
#ifdef OB_BUILD_SHARED_STORAGE
  return OB_INVALID_ID != tenant_id_
      && ls_id_.is_valid_with_tenant(tenant_id_)
      && tablet_id_.is_valid_with_tenant(tenant_id_);
#else
  return OB_INVALID_ID != tenant_id_
      && ls_id_.is_valid_with_tenant(tenant_id_)
      && tablet_id_.is_valid_with_tenant(tenant_id_)
      && server_.is_valid();
#endif
}

bool ObTabletReplicaChecksumItem::is_valid() const
{
  return is_key_valid()
       && column_meta_.is_valid()
       && is_valid_data_checksum_type(data_checksum_type_);
}

bool ObTabletReplicaChecksumItem::is_same_tablet(const ObTabletReplicaChecksumItem &other) const
{
  return is_key_valid()
      && other.is_key_valid()
      && tenant_id_ == other.tenant_id_
      && ls_id_ == other.ls_id_
      && tablet_id_ == other.tablet_id_;
}

int ObTabletReplicaChecksumItem::check_data_checksum_type(bool &is_cs_replica) const
{
  int ret = OB_SUCCESS;
  is_cs_replica = false;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid checksum item", K(ret), KPC(this));
  } else if (is_column_store_data_checksum_type(data_checksum_type_)) {
    is_cs_replica = true;
  }
  return ret;
}

void ObTabletReplicaChecksumItem::set_data_checksum_type(const bool is_cs_replica)
{
  if (is_cs_replica) {
    data_checksum_type_ = ObDataChecksumType::DATA_CHECKSUM_COLUMN_STORE_WITH_NORMAL_COLUMN;
  } else {
    data_checksum_type_ = ObDataChecksumType::DATA_CHECKSUM_NORMAL_WITH_NORMAL_COLUMN;
  }
}


int ObTabletReplicaChecksumItem::verify_column_checksum(const ObTabletReplicaChecksumItem &other) const
{
  int ret = OB_SUCCESS;
  bool column_meta_equal = false;
  bool is_cs_replica_flag1 = false;
  bool is_cs_replica_flag2 = false;
  if (OB_UNLIKELY(compaction_scn_ != other.compaction_scn_)) {
    // do nothing
  } else if (OB_FAIL(column_meta_.check_equal(other.column_meta_, column_meta_equal))) {
    LOG_WARN("fail to check column meta equal", KR(ret), K(other), K(*this));
  } else if (column_meta_equal) {
    // do nothing
  } else if (OB_FAIL(check_data_checksum_type(is_cs_replica_flag1))) {
    LOG_WARN("fail to check data checksum type", KR(ret), KPC(this));
  } else if (OB_FAIL(other.check_data_checksum_type(is_cs_replica_flag2))) {
    LOG_WARN("fail to check data checksum type", KR(ret), K(other));
  } else if (is_cs_replica_flag1 == is_cs_replica_flag2) {
    ret = OB_CHECKSUM_ERROR; // compaction between the same replica type can be compared
  } else if (OB_FAIL(verify_column_checksum_between_diffrent_replica(other))) {
    LOG_WARN("fail to verify column checksum between diffrent replica", KR(ret), K(other), K(*this));
  }
  return ret;
}

int ObTabletReplicaChecksumItem::verify_column_checksum_between_diffrent_replica(const ObTabletReplicaChecksumItem &other) const
{
  int ret = OB_SUCCESS;
  ObFreezeInfo boundary_freeze_info;
  ObFreezeInfo to_check_freeze_info;
  if (OB_FAIL(MTL(ObTenantFreezeInfoMgr *)->get_lower_bound_freeze_info_before_snapshot_version(compaction_scn_.get_val_for_tx(), boundary_freeze_info))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get boundary freeze info", K(ret), K_(compaction_scn));
    }
  } else if (boundary_freeze_info.is_valid()) {
    ret = OB_CHECKSUM_ERROR; // it is compacted in lob column checksum fixed version
    LOG_WARN("failed to check column checksum", K(ret), K(boundary_freeze_info));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(MTL(ObTenantFreezeInfoMgr *)->get_freeze_info_by_snapshot_version(compaction_scn_.get_val_for_tx(), to_check_freeze_info))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get freeze info", K(ret), K_(compaction_scn));
    }
  } else if (!to_check_freeze_info.is_valid()) {
  } else {
    ret = OB_CHECKSUM_ERROR; // it is compacted in lob column checksum fixed version
    LOG_WARN("failed to check column checksum", K(ret), K(to_check_freeze_info));
  }
  return ret;
}


int ObTabletReplicaChecksumItem::assign(const ObTabletReplicaChecksumItem &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    if (OB_FAIL(set_tenant_id(other.tenant_id_))) {
      LOG_WARN("failed to set tenant id", KR(ret), K(other));
    } else if (OB_FAIL(column_meta_.assign(other.column_meta_))) {
      LOG_WARN("fail to assign column meta", KR(ret), K(other));
    } else {
      tablet_id_ = other.tablet_id_;
      ls_id_ = other.ls_id_;
      server_ = other.server_;
      row_count_ = other.row_count_;
      compaction_scn_ = other.compaction_scn_;
      data_checksum_ = other.data_checksum_;
      data_checksum_type_ = other.data_checksum_type_;
      co_base_snapshot_version_ = other.co_base_snapshot_version_;
    }
  }
  return ret;
}

int ObTabletReplicaChecksumItem::set_tenant_id(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant id", KR(ret), K(tenant_id));
  } else {
    tenant_id_ = tenant_id;
    column_meta_.column_checksums_.set_attr(ObMemAttr(tenant_id, "RepCkmItem"));
  }
  return ret;
}

/****************************** ObTabletReplicaChecksumOperator ******************************/

int ObTabletReplicaChecksumOperator::init()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ not initialized", K(ret));
  } else if (OB_FAIL(storage_.init(GCTX.meta_db_pool_))) {
    LOG_WARN("failed to init storage", K(ret));
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::batch_update_with_trans(
    ObSQLiteConnection *conn,
    const uint64_t tenant_id,
    const common::ObIArray<ObTabletReplicaChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid connection", K(ret));
  } else if (OB_UNLIKELY((OB_INVALID_TENANT_ID == tenant_id) || (items.count() <= 0))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), "items count", items.count());
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    // Use SQLite storage within the transaction
    // Note: The transaction is managed by the caller (ObTabletTableUpdater)
    const char *upsert_sql =
      "INSERT INTO __all_tablet_replica_checksum "
      "(tablet_id, compaction_scn, "
      " row_count, data_checksum, column_checksums, b_column_checksums, "
      " data_checksum_type, co_base_snapshot_version) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(tablet_id) DO UPDATE SET "
      "compaction_scn = excluded.compaction_scn, "
      "row_count = excluded.row_count, "
      "data_checksum = excluded.data_checksum, "
      "column_checksums = excluded.column_checksums, "
      "b_column_checksums = excluded.b_column_checksums, "
      "data_checksum_type = excluded.data_checksum_type, "
      "co_base_snapshot_version = excluded.co_base_snapshot_version;";

    ObSQLiteStmt *stmt = nullptr;
    if (OB_FAIL(conn->prepare_execute(upsert_sql, stmt))) {
      LOG_WARN("failed to prepare execute", K(ret));
    } else {
      common::ObArenaAllocator allocator;
      for (int64_t i = 0; OB_SUCC(ret) && i < items.count(); ++i) {
        const ObTabletReplicaChecksumItem &item = items.at(i);
        // Convert column_meta to string
        common::ObString column_checksums_str;
        common::ObString b_column_checksums_str;
        if (item.column_meta_.is_valid()) {
          if (OB_FAIL(get_visible_column_meta(item.column_meta_, allocator, column_checksums_str))) {
            LOG_WARN("failed to get visible column meta", K(ret));
          } else {
            common::ObObj obj;
            if (OB_FAIL(item.column_meta_.get_str_obj(item.data_checksum_type_, allocator, obj, b_column_checksums_str))) {
              LOG_WARN("failed to get hex column meta str", K(ret));
            }
          }
        }

        if (OB_SUCC(ret)) {
          auto binder = [&](ObSQLiteBinder &b) -> int {
            b.bind_int64(item.tablet_id_.id());
            b.bind_int64(item.compaction_scn_.get_val_for_inner_table_field());
            b.bind_int64(item.row_count_);
            b.bind_int64(item.data_checksum_);
              if (column_checksums_str.empty()) {
                b.bind_text("", 0);
              } else {
                b.bind_text(column_checksums_str.ptr(), column_checksums_str.length());
              }
              if (b_column_checksums_str.empty()) {
                b.bind_blob(nullptr, 0);
              } else {
                b.bind_blob(b_column_checksums_str.ptr(), b_column_checksums_str.length());
              }
              b.bind_int64(static_cast<int64_t>(item.data_checksum_type_));
              b.bind_int64(item.co_base_snapshot_version_.get_val_for_inner_table_field());
              return OB_SUCCESS;
            };

          if (OB_FAIL(conn->step_execute(stmt, binder))) {
            LOG_WARN("failed to step execute", K(ret), K(i));
          }
        }
      }

      // Finalize statement (but don't commit/rollback - caller manages transaction)
      conn->finalize_execute(stmt);
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::batch_remove_with_trans(
    ObSQLiteConnection *conn,
    const uint64_t tenant_id,
    const common::ObIArray<share::ObTabletReplica> &tablet_replicas)
{
  int ret = OB_SUCCESS;
  const int64_t replicas_count = tablet_replicas.count();
  if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid connection", K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id || replicas_count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), "tablet_replica cnt", replicas_count);
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    const char *delete_sql =
      "DELETE FROM __all_tablet_replica_checksum "
      "WHERE tablet_id = ?;";

    ObSQLiteStmt *stmt = nullptr;
    if (OB_FAIL(conn->prepare_execute(delete_sql, stmt))) {
      LOG_WARN("failed to prepare execute", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < replicas_count; ++i) {
        const ObTabletReplica &replica = tablet_replicas.at(i);
        if (replica.primary_keys_are_valid()) {
          auto binder = [&](ObSQLiteBinder &b) -> int {
            b.bind_int64(replica.get_tablet_id().id());
            return OB_SUCCESS;
          };

          if (OB_FAIL(conn->step_execute(stmt, binder))) {
            LOG_WARN("failed to step execute", K(ret), K(i));
          }
        }
      }
      // Finalize statement (but don't commit/rollback - caller manages transaction)
      conn->finalize_execute(stmt);
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::remove_residual_checksum(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const ObAddr &server,
    const int64_t limit,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id)
                  || is_virtual_tenant_id(tenant_id)
                  || !server.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(server));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    ret = storage_.remove_residual(tenant_id, server, limit, affected_rows);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to remove residual checksum", K(ret), K(tenant_id), K(server));
    } else if (affected_rows > 0) {
      LOG_INFO("finish to remove residual checksum", KR(ret), K(tenant_id), K(affected_rows));
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::get_tablets_replica_checksum(
    const uint64_t tenant_id,
    const ObIArray<compaction::ObTabletCheckInfo> &pairs,
    ObReplicaCkmArray &tablet_replica_checksum_items)
{
  int ret = OB_SUCCESS;
  const int64_t pairs_cnt = pairs.count();
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id) || pairs_cnt <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(tenant_id), K(pairs));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    // Convert ObTabletCheckInfo to ObTabletLSPair
    ObSEArray<ObTabletLSPair, 64> tablet_ls_pairs;
    for (int64_t i = 0; OB_SUCC(ret) && i < pairs_cnt; ++i) {
      const compaction::ObTabletCheckInfo &check_info = pairs.at(i);
      if (OB_FAIL(tablet_ls_pairs.push_back(ObTabletLSPair(check_info.get_tablet_id(), check_info.get_ls_id())))) {
        LOG_WARN("failed to push back tablet ls pair", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      ret = storage_.batch_get(tenant_id, tablet_ls_pairs, SCN(), tablet_replica_checksum_items, false);
      if (OB_FAIL(ret)) {
        LOG_WARN("failed to batch get from storage", K(ret), K(tenant_id));
      } else {
        LOG_TRACE("success to get tablet replica checksum items", KR(ret), K(pairs_cnt));
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::batch_get(
    const uint64_t tenant_id,
    const ObIArray<ObTabletLSPair> &pairs,
    const SCN &compaction_scn,
    ObISQLClient &sql_proxy,
    ObReplicaCkmArray &items,
    const bool include_larger_than,
    const int32_t group_id)
{
  int ret = OB_SUCCESS;
  items.reset();
  const int64_t pairs_cnt = pairs.count();
  if (OB_UNLIKELY(pairs_cnt < 1 || OB_INVALID_TENANT_ID == tenant_id || group_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(pairs_cnt), K(group_id));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    ret = storage_.batch_get(tenant_id, pairs, compaction_scn, items, include_larger_than);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to batch get from storage", K(ret), K(tenant_id));
    }
  }
  return ret;
}

// inner_batch_get_by_sql_ removed - no longer used, replaced by SQLite storage

int ObTabletReplicaChecksumOperator::construct_tablet_replica_checksum_items_(
    sqlclient::ObMySQLResult &res,
    ObReplicaCkmArray &items)
{
  int ret = OB_SUCCESS;
  ObTabletReplicaChecksumItem item;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(res.next())) {
      if (OB_ITER_END != ret) {
        LOG_WARN("fail to get next result", KR(ret));
      }
    } else {
      item.reset();
      if (OB_FAIL(construct_tablet_replica_checksum_item_(res, item))) {
        LOG_WARN("fail to construct tablet checksum item", KR(ret));
#ifdef ERRSIM
      } else if (item.get_tablet_id().id() > ObTabletID::MIN_USER_TABLET_ID) {
          ret = OB_E(EventTable::EN_RS_CANT_GET_ALL_TABLET_CHECKSUM) ret;
          if (OB_FAIL(ret)) { // skip push item
            LOG_INFO("ERRSIM EN_RS_CANT_GET_ALL_TABLET_CHECKSUM", K(ret), K(items), K(item));
          } else if (OB_FAIL(items.push_back(item))) {
            LOG_WARN("fail to push back checksum item", KR(ret), K(item));
          }
#endif
      } else if (OB_FAIL(items.push_back(item))) {
        LOG_WARN("fail to push back checksum item", KR(ret), K(item));
      }
    }
  }
  if (OB_ITER_END == ret) {
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::construct_tablet_replica_checksum_items_(
    sqlclient::ObMySQLResult &res,
    common::ObIArray<ObTabletReplicaChecksumItem> &items,
    int64_t &tablet_items_cnt)
{
  int ret = OB_SUCCESS;
  ObTabletReplicaChecksumItem item;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(res.next())) {
      if (OB_ITER_END != ret) {
        LOG_WARN("fail to get next result", KR(ret));
      }
    } else {
      item.reset();
      if (OB_FAIL(construct_tablet_replica_checksum_item_(res, item))) {
        LOG_WARN("fail to construct tablet checksum item", KR(ret));
      } else if (items.empty()
        || item.tablet_id_ != items.at(items.count() - 1).tablet_id_) {
        ++tablet_items_cnt;
      }
      if (FAILEDx(items.push_back(item))) {
        LOG_WARN("fail to push back checksum item", KR(ret), K(item));
      }
    }
  }
  if (OB_ITER_END == ret) {
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::construct_tablet_replica_checksum_item_(
    sqlclient::ObMySQLResult &res,
    ObTabletReplicaChecksumItem &item)
{
  int ret = OB_SUCCESS;
  int64_t int_tablet_id = -1;
  uint64_t compaction_scn_val = 0;
  const uint64_t tenant_id = MTL_ID();
  int64_t data_checksum_type = 0;
  uint64_t co_base_snapshot_version_val = 0;
  ObString b_column_meta_str;

  (void)GET_COL_IGNORE_NULL(res.get_int, "tablet_id", int_tablet_id);
  (void)GET_COL_IGNORE_NULL(res.get_uint, "compaction_scn", compaction_scn_val);
  (void)GET_COL_IGNORE_NULL(res.get_int, "row_count", item.row_count_);
  (void)GET_COL_IGNORE_NULL(res.get_int, "data_checksum", item.data_checksum_);
  (void)GET_COL_IGNORE_NULL(res.get_varchar, "b_column_checksums", b_column_meta_str);

  (void)GET_COL_IGNORE_NULL(res.get_int, "data_checksum_type", data_checksum_type);
  if (is_valid_data_checksum_type(static_cast<ObDataChecksumType>(data_checksum_type))) {
    item.data_checksum_type_ = static_cast<ObDataChecksumType>(data_checksum_type);
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid data checksum type", KR(ret), K(data_checksum_type));
  }

  if (OB_FAIL(ret)) {
  } else {
    (void)GET_COL_IGNORE_NULL(res.get_uint, "co_base_snapshot_version", co_base_snapshot_version_val);
    if (OB_FAIL(item.co_base_snapshot_version_.convert_for_inner_table_field(co_base_snapshot_version_val))) {
      LOG_WARN("fail to convert val to SCN", KR(ret), K(co_base_snapshot_version_val));
    }
  }

  if (FAILEDx(item.compaction_scn_.convert_for_inner_table_field(compaction_scn_val))) {
    LOG_WARN("fail to convert val to SCN", KR(ret), K(compaction_scn_val));
  } else if (OB_FAIL(item.set_tenant_id(tenant_id))) {
    LOG_WARN("failed to set tenant id", KR(ret), K(tenant_id));
  } else {
    item.tablet_id_ = (uint64_t)int_tablet_id;
    item.ls_id_ = ObLSID::SYS_LS_ID;
    item.server_ = GCTX.self_addr();
    if (OB_FAIL(item.column_meta_.set_with_str(item.data_checksum_type_, b_column_meta_str))) {
      LOG_WARN("fail to set column meta", KR(ret), K(compaction_scn_val), K(b_column_meta_str));
    }
#ifdef ERRSIM
    if (OB_SUCC(ret)) {
      ret = OB_E(EventTable::EN_MOCK_LARGE_COLUMN_META) ret;
      if (OB_FAIL(ret)) {
        ret = OB_SUCCESS;
        if (OB_FAIL(recover_mock_column_meta(item.column_meta_))) {
          LOG_WARN("fail to recover mock large column meta", KR(ret));
        } else {
          LOG_INFO("ERRSIM EN_MOCK_LARGE_COLUMN_META", K(ret));
        }
      }
    }
#endif
  }

  LOG_TRACE("construct tablet checksum item", KR(ret), K(item));
  return ret;
}


// inner_batch_insert_or_update_by_sql_ removed - no longer used, replaced by SQLite storage
// OB_BUILD_SHARED_STORAGE related functions removed


int ObTabletReplicaChecksumOperator::get_tablet_replica_checksum_items(
    const uint64_t tenant_id,
    ObMySQLProxy &sql_proxy,
    const SCN &compaction_scn,
    const ObIArray<ObTabletLSPair> &tablet_pairs,
    ObReplicaCkmArray &items)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
  } else if (OB_FAIL(batch_get(tenant_id, tablet_pairs, compaction_scn,
        sql_proxy, items, false/*include_larger_than*/,
        share::OBCG_DEFAULT))) {
    LOG_WARN("fail to batch get tablet checksum item", KR(ret), K(tenant_id), K(compaction_scn),
      "pairs_count", tablet_pairs.count());
  } else if (items.get_tablet_cnt() < tablet_pairs.count()) {
    ret = OB_ITEM_NOT_MATCH;
    LOG_WARN("fail to get tablet replica checksum items", KR(ret), K(tenant_id), K(compaction_scn),
      K(items));
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::get_visible_column_meta(
    const ObTabletReplicaReportColumnMeta &column_meta,
    common::ObIAllocator &allocator,
    common::ObString &column_meta_visible_str)
{
  int ret = OB_SUCCESS;
  char *column_meta_str = NULL;
  const int64_t length = column_meta.get_string_length() * 2;
  int64_t pos = 0;

  if (OB_UNLIKELY(!column_meta.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("column meta is not valid", KR(ret), K(column_meta));
  } else if (OB_UNLIKELY(length > OB_MAX_LONGTEXT_LENGTH + 1)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("column meta too long", KR(ret), K(length), K(column_meta));
  } else if (OB_ISNULL(column_meta_str = static_cast<char *>(allocator.alloc(length)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc buf", KR(ret), K(length));
  } else if (FALSE_IT(pos = column_meta.get_string(column_meta_str, length))) {
    //nothing
  } else if (OB_UNLIKELY(pos >= length)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("size overflow", KR(ret), K(pos), K(length));
  } else {
    column_meta_visible_str.assign(column_meta_str, static_cast<int32_t>(pos));
  }
  return ret;
}

// range_get_ removed - no longer used, replaced by SQLite storage

int ObTabletReplicaChecksumOperator::range_get(
      const uint64_t tenant_id,
      const common::ObTabletID &start_tablet_id,
      const common::ObTabletID &end_tablet_id,
      const int64_t compaction_scn,
      common::ObISQLClient &sql_proxy,
      ObIArray<ObTabletReplicaChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(start_tablet_id > end_tablet_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(start_tablet_id), K(end_tablet_id));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    // Use SQLite storage - range_get doesn't support end_tablet_id, so we need to filter manually
    // For now, use a large range_size to get all items and filter
    const int64_t large_range = INT64_MAX;
    int64_t tablet_cnt = 0;
    ret = storage_.range_get(tenant_id, start_tablet_id, large_range, items, tablet_cnt);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to range get from storage", K(ret), K(tenant_id), K(start_tablet_id));
    } else {
      // Filter by end_tablet_id and compaction_scn
      ObSEArray<ObTabletReplicaChecksumItem, 64> filtered_items;
      for (int64_t i = 0; OB_SUCC(ret) && i < items.count(); ++i) {
        const ObTabletReplicaChecksumItem &item = items.at(i);
        if (item.tablet_id_ >= start_tablet_id && item.tablet_id_ <= end_tablet_id &&
            item.compaction_scn_.get_val_for_inner_table_field() == compaction_scn) {
          if (OB_FAIL(filtered_items.push_back(item))) {
            LOG_WARN("failed to push back item", K(ret));
          }
        }
      }
      if (OB_SUCC(ret)) {
        items.reset();
        for (int64_t i = 0; OB_SUCC(ret) && i < filtered_items.count(); ++i) {
          if (OB_FAIL(items.push_back(filtered_items.at(i)))) {
            LOG_WARN("failed to push back item", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::range_get(
      const uint64_t tenant_id,
      const common::ObTabletID &start_tablet_id,
      const int64_t range_size,
      const int32_t group_id,
      common::ObISQLClient &sql_proxy,
      ObIArray<ObTabletReplicaChecksumItem> &items,
      int64_t &tablet_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(range_size <= 0 || group_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(range_size), K(group_id));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    ret = storage_.range_get(tenant_id, start_tablet_id, range_size, items, tablet_cnt);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to range get from storage", K(ret), K(tenant_id), K(start_tablet_id), K(range_size));
    } else if (OB_UNLIKELY(items.count() > range_size)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get too much tablets", KR(ret), K(range_size), "items count", items.count());
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::construct_tablet_id_list(const ObIArray<ObTabletID> &tablet_ids, ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  for (int64_t idx = 0; OB_SUCC(ret) && (idx < tablet_ids.count()); ++idx) {
    const ObTabletID &tablet_id = tablet_ids.at(idx);
    if (OB_FAIL(sql.append_fmt("%s %ld", 0 == idx ? "" : ",", tablet_id.id()))) {
      LOG_WARN("fail to assign sql", KR(ret), K(tablet_id));
    }
  } // end for
  return ret;
}

int ObTabletReplicaChecksumOperator::multi_get(
    const uint64_t tenant_id,
    const ObIArray<ObTabletID> &tablet_ids,
    const int64_t compaction_scn,
    common::ObISQLClient &sql_proxy,
    ObIArray<ObTabletReplicaChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  items.reset();
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id) || compaction_scn < 0 || tablet_ids.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(compaction_scn), K(tablet_ids));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    // Convert tablet_ids to tablet-ls pairs (we need ls_id, but multi_get only provides tablet_id)
    // For now, we'll query all replicas for these tablets and filter by compaction_scn
    ObSEArray<ObTabletLSPair, 64> tablet_ls_pairs;
    // Since we don't have ls_id, we'll need to query all and filter
    // For efficiency, we'll use range_get with a large range and filter
    // But a better approach is to query by tablet_id list
    const char *select_sql =
      "SELECT tablet_id, compaction_scn, "
      "       row_count, data_checksum, column_checksums, b_column_checksums, "
      "       data_checksum_type, co_base_snapshot_version "
      "FROM __all_tablet_replica_checksum "
      "WHERE compaction_scn = ? AND tablet_id IN (";

    ObSqlString sql;
    if (OB_FAIL(sql.append(select_sql))) {
      LOG_WARN("failed to append sql", K(ret));
    } else {
      // Build IN clause
      for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
        if (OB_FAIL(sql.append_fmt("%s%ld", i == 0 ? "" : ",", tablet_ids.at(i).id()))) {
          LOG_WARN("failed to append tablet_id", K(ret));
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(sql.append(") ORDER BY tablet_id ASC;"))) {
        LOG_WARN("failed to append sql", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      auto binder = [&](ObSQLiteBinder &b) -> int {
        b.bind_int64(compaction_scn);
        return OB_SUCCESS;
      };

      common::ObArenaAllocator allocator;
      auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
        ObTabletReplicaChecksumItem item;
        int64_t tablet_id_val = reader.get_int64();
        uint64_t compaction_scn_val = reader.get_int64();
        int64_t row_count = reader.get_int64();
        int64_t data_checksum = reader.get_int64();
        int column_checksums_len = 0;
        int b_column_checksums_len = 0;
        const char *column_checksums_str = reader.get_text(&column_checksums_len);
        UNUSED(column_checksums_str);
        const void *b_column_checksums_blob = reader.get_blob(&b_column_checksums_len);
        int64_t data_checksum_type = reader.get_int64();
        uint64_t co_base_snapshot_version_val = reader.get_int64();

        item.tenant_id_ = OB_SYS_TENANT_ID;
        item.tablet_id_ = ObTabletID(tablet_id_val);
        item.ls_id_ = ObLSID::SYS_LS_ID;
        item.server_ = GCTX.self_addr();
        item.compaction_scn_.convert_for_inner_table_field(compaction_scn_val);
        item.row_count_ = row_count;
        item.data_checksum_ = data_checksum;
        item.data_checksum_type_ = static_cast<ObDataChecksumType>(data_checksum_type);
        item.co_base_snapshot_version_.convert_for_inner_table_field(co_base_snapshot_version_val);

        // Parse b_column_checksums blob
        if (OB_NOT_NULL(b_column_checksums_blob) && b_column_checksums_len > 0) {
          common::ObString b_column_checksums_obstr(b_column_checksums_len, static_cast<const char *>(b_column_checksums_blob));
          int tmp_ret = item.column_meta_.set_with_str(item.data_checksum_type_, b_column_checksums_obstr);
          if (OB_SUCCESS != tmp_ret) {
            LOG_WARN("failed to set column meta with b_column_checksums blob, skip invalid data",
                     K(tmp_ret), K(b_column_checksums_obstr), K(item.tablet_id_), K(item.ls_id_));
            item.column_meta_.reset();
          }
        }

        if (OB_SUCC(ret) && OB_FAIL(items.push_back(item))) {
          LOG_WARN("failed to push back item", K(ret));
        }
        return ret;
      };

      // Use storage_ connection pool
      if (OB_ISNULL(GCTX.meta_db_pool_)) {
        ret = OB_NOT_INIT;
        LOG_WARN("meta_db_pool_ not initialized", K(ret));
      } else {
        ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
        if (!guard) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to acquire connection", K(ret));
        } else if (OB_FAIL(guard->query(sql.ptr(), binder, row_processor))) {
          if (OB_ENTRY_NOT_EXIST != ret) {
            LOG_WARN("failed to query", K(ret));
          } else {
            ret = OB_SUCCESS; // No rows is acceptable
          }
        }
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::get_min_compaction_scn(
    const uint64_t tenant_id,
    SCN &min_compaction_scn)
{
  int ret = OB_SUCCESS;
  const int64_t start_time_us = ObTimeUtil::current_time();
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    uint64_t min_compaction_scn_val = UINT64_MAX;
    if (OB_FAIL(storage_.get_min_compaction_scn(tenant_id, min_compaction_scn_val))) {
      LOG_WARN("failed to get min compaction scn from storage", K(ret), K(tenant_id));
    } else if (OB_FAIL(min_compaction_scn.convert_for_inner_table_field(min_compaction_scn_val))) {
      LOG_WARN("fail to convert uint64_t to SCN", KR(ret), K(min_compaction_scn_val));
    }
    LOG_INFO("finish to get min_compaction_scn", KR(ret), K(tenant_id), K(min_compaction_scn),
             "cost_time_us", ObTimeUtil::current_time() - start_time_us);
  }
  return ret;
}

// batch_iterate_replica_checksum_range_ removed - no longer used
// batch_check_tablet_checksum_in_range_ removed - no longer used

// ----------------------- ObTabletDataChecksumChecker -----------------------
ObTabletDataChecksumChecker::ObTabletDataChecksumChecker()
  : normal_ckm_item_(nullptr),
    cs_replica_ckm_items_()
{
  cs_replica_ckm_items_.set_attr(ObMemAttr(MTL_ID(), "DataCkmChecker"));
}

ObTabletDataChecksumChecker::~ObTabletDataChecksumChecker()
{
  reset();
}

void ObTabletDataChecksumChecker::reset()
{
  normal_ckm_item_ = nullptr;
  cs_replica_ckm_items_.reset();
}

int ObTabletDataChecksumChecker::check_data_checksum(const ObTabletReplicaChecksumItem& curr_item)
{
  int ret = OB_SUCCESS;
  bool is_cs_replica = false;
  if (OB_FAIL(curr_item.check_data_checksum_type(is_cs_replica))) {
    LOG_WARN("fail to check data checksum type", KR(ret), K(curr_item));
  } else if (is_cs_replica) {
    if (curr_item.compaction_scn_.is_max()) {
    } else {
      // check data checksum between cs replicas with the same co base snapshot version
      for (int64_t idx = 0; OB_SUCC(ret) && idx < cs_replica_ckm_items_.count(); idx++) {
        const ObTabletReplicaChecksumItem *item = cs_replica_ckm_items_.at(idx);
        if (OB_ISNULL(item)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid null item", K(ret), K(idx), K_(cs_replica_ckm_items));
        } else if (OB_UNLIKELY(curr_item.compaction_scn_ == item->compaction_scn_
                            && curr_item.co_base_snapshot_version_ == item->co_base_snapshot_version_
                            && curr_item.data_checksum_ != item->data_checksum_)) {
          ret = OB_CHECKSUM_ERROR;
          LOG_WARN("find cs replica data checksum error", K(ret), K(curr_item), KPC(item));
        }
      }
      if (FAILEDx(cs_replica_ckm_items_.push_back(&curr_item))) {
        LOG_WARN("failed to push back item", K(ret), K_(cs_replica_ckm_items));
      }
    }
  } else {
    if (OB_ISNULL(normal_ckm_item_)) {
      normal_ckm_item_ = &curr_item;
    } else if (normal_ckm_item_->compaction_scn_ != curr_item.compaction_scn_) {
      LOG_INFO("no need to check data checksum", K(curr_item), KPC(this));
    } else if (normal_ckm_item_->data_checksum_ != curr_item.data_checksum_) {
      ret = OB_CHECKSUM_ERROR;
      LOG_WARN("find data checksum error", K(ret), K(curr_item), KPC_(normal_ckm_item));
    }
  }
  return ret;
}

int ObTabletDataChecksumChecker::set_data_checksum(const ObTabletReplicaChecksumItem& curr_item)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(normal_ckm_item_)) {
    bool is_cs_replica = false;
    if (OB_FAIL(curr_item.check_data_checksum_type(is_cs_replica))) {
      LOG_WARN("fail to check data checksum type", KR(ret), K(curr_item));
    } else if (!is_cs_replica) {
      normal_ckm_item_ = &curr_item;
    }
  }
  return ret;
}

} // share
} // oceanbase
