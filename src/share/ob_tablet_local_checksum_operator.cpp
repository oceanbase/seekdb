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

// Local tablet checksum persistence and lookup.

#include "share/ob_tablet_local_checksum_operator.h"
#include "share/storage/ob_tablet_local_checksum_table_storage.h"
#include "share/storage/ob_sqlite_connection.h"
namespace oceanbase
{
namespace share
{
using namespace oceanbase::common;

// Static storage instance
ObTabletLocalChecksumTableStorage ObTabletLocalChecksumOperator::storage_;

ObTabletColumnChecksumMeta::ObTabletColumnChecksumMeta()
  : compat_version_(0),
    checksum_method_(0),
    checksum_bytes_(0),
    column_checksums_(),
    is_inited_(false)
{}

ObTabletColumnChecksumMeta::~ObTabletColumnChecksumMeta()
{
  reset();
}

void ObTabletColumnChecksumMeta::reset()
{
  is_inited_ = false;
  compat_version_ = 0;
  checksum_method_ = 0;
  checksum_bytes_ = 0;
  column_checksums_.reset();
}

bool ObTabletColumnChecksumMeta::is_valid() const
{
  return is_inited_ && column_checksums_.count() > 0;
}

int ObTabletColumnChecksumMeta::init(const ObIArray<int64_t> &column_checksums)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTabletColumnChecksumMeta inited twice", KR(ret), K(*this));
  } else if (column_checksums.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_FAIL(column_checksums_.assign(column_checksums))) {
  } else {
    checksum_bytes_ = (sizeof(int16_t) + sizeof(int64_t) + sizeof(int8_t)) * 2;
    checksum_method_ = 0; // TODO
    is_inited_ = true;
  }
  return ret;
}

int ObTabletColumnChecksumMeta::assign(const ObTabletColumnChecksumMeta &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    if (other.column_checksums_.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret));
    } else if (OB_FAIL(column_checksums_.assign(other.column_checksums_))) {
    } else {
      compat_version_ = other.compat_version_;
      checksum_method_ = other.checksum_method_;
      checksum_bytes_ = other.checksum_bytes_;
      is_inited_ = true;
    }
  }
  return ret;
}

int ObTabletColumnChecksumMeta::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  int64_t serialize_size = get_serialize_size();
  if (OB_UNLIKELY(NULL == buf) || (serialize_size > buf_len)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments.", KP(buf), KR(ret), K(serialize_size), K(buf_len));
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, pos, MAGIC_NUMBER))) {
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, compat_version_))) {
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, checksum_method_))) {
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, checksum_bytes_))) {
  } else if (OB_FAIL(column_checksums_.serialize(buf, buf_len, pos))) {
  }
  return ret;
}

int64_t ObTabletColumnChecksumMeta::get_serialize_size() const
{
  int64_t len = 0;
  len += serialization::encoded_length_i64(MAGIC_NUMBER);
  len += serialization::encoded_length_i8(compat_version_);
  len += serialization::encoded_length_i8(checksum_method_);
  len += serialization::encoded_length_i8(checksum_bytes_);
  len += column_checksums_.get_serialize_size();
  return len;
}

int ObTabletColumnChecksumMeta::deserialize(const char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t magic_number = 0;
  reset();
  if (OB_ISNULL(buf) || (buf_len < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", KR(ret), K(buf), K(buf_len));
  } else if (OB_FAIL(serialization::decode_i64(buf, buf_len, pos, &magic_number))) {
  } else if (OB_UNLIKELY(MAGIC_NUMBER != magic_number)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid column checksum metadata magic number", KR(ret), K(magic_number));
  } else if (OB_FAIL(serialization::decode_i8(buf, buf_len, pos, &compat_version_))) {
  } else if (OB_FAIL(serialization::decode_i8(buf, buf_len, pos, &checksum_method_))) {
  } else if (OB_FAIL(serialization::decode_i8(buf, buf_len, pos, &checksum_bytes_))) {
  } else if (OB_FAIL(column_checksums_.deserialize(buf, buf_len, pos))) {
  } else {
    is_inited_ = true;
  }
  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int64_t ObTabletColumnChecksumMeta::get_string_length() const
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

int64_t ObTabletColumnChecksumMeta::get_string(char *buf, const int64_t buf_len) const
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

int ObTabletColumnChecksumMeta::check_checksum(
    const ObTabletColumnChecksumMeta &other,
    const int64_t pos, bool &is_equal) const
{
  int ret = OB_SUCCESS;
  is_equal = true;
  const int64_t col_ckm_cnt = column_checksums_.count();
  const int64_t other_col_ckm_cnt = other.column_checksums_.count();
  if ((pos < 0) || (pos >= col_ckm_cnt) || (pos >= other_col_ckm_cnt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", KR(ret), K(pos), K(col_ckm_cnt), K(other_col_ckm_cnt),
      K(column_checksums_), K(other.column_checksums_));
  } else if (column_checksums_.at(pos) != other.column_checksums_.at(pos)) {
    is_equal = false;
    LOG_ERROR("column checksum is not equal!", K(pos), "col_ckm", column_checksums_.at(pos),
      "other_col_ckm", other.column_checksums_.at(pos), K(col_ckm_cnt), K(other_col_ckm_cnt),
      K(column_checksums_), K(other.column_checksums_));
  }
  return ret;
}

int ObTabletColumnChecksumMeta::check_all_checksums(
    const ObTabletColumnChecksumMeta &other,
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
      }
    }
  }
  return ret;
}

int ObTabletColumnChecksumMeta::check_equal(
    const ObTabletColumnChecksumMeta &other,
    bool &is_equal) const
{
  int ret = OB_SUCCESS;
  is_equal = true;
  if (compat_version_ != other.compat_version_) {
    is_equal = false;
    LOG_WARN("compat version is not equal !", K(*this), K(other));
  } else if (checksum_method_ != other.checksum_method_) {
    is_equal = false;
    LOG_ERROR("checksum method is different !", K(*this), K(other));
  } else if (OB_FAIL(check_all_checksums(other, is_equal))) {
  }
  return ret;
}

int ObTabletColumnChecksumMeta::set_with_str(const ObString &str)
{
  int ret = set_with_serialize_str(str);
  if (OB_FAIL(ret)) {
  }
  return ret;
}

int ObTabletColumnChecksumMeta::set_with_str(
    const ObDataChecksumType type,
    const ObString &str)
{
  int ret = OB_SUCCESS;
  if (!is_valid_data_checksum_type(type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid column checksum type", K(type));
  } else if (is_normal_column_checksum_type(type)) {
    if (OB_FAIL(set_with_serialize_str(str))) {
    }
  } else if (OB_FAIL(set_with_hex_str(str))) {
  }
  return ret;
}

int ObTabletColumnChecksumMeta::set_with_hex_str(const common::ObString &hex_str)
{
  int ret = OB_SUCCESS;
  const int64_t hex_str_len = hex_str.length();
  if (hex_str_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(hex_str_len), K(hex_str));
  } else {
    const int64_t deserialize_size = ObTabletColumnChecksumMeta::MAX_OCCUPIED_BYTES;
    int64_t deserialize_pos = 0;
    char *deserialize_buf = NULL;
    ObArenaAllocator allocator;

    if (OB_ISNULL(deserialize_buf = static_cast<char *>(allocator.alloc(deserialize_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", KR(ret), K(deserialize_size));
    } else if (OB_FAIL(hex_to_cstr(hex_str.ptr(), hex_str_len, deserialize_buf, deserialize_size))) {
    } else if (OB_FAIL(deserialize(deserialize_buf, deserialize_size, deserialize_pos))) {
    } else if (deserialize_pos > deserialize_size) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("deserialize size overflow", KR(ret), K(deserialize_pos), K(deserialize_size));
    }
  }
  return ret;
}

int ObTabletColumnChecksumMeta::set_with_serialize_str(const common::ObString &serialize_str)
{
  int ret = OB_SUCCESS;
  const int64_t serialize_len = serialize_str.length();
  int64_t pos = 0;
  if (serialize_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(serialize_len), K(serialize_str));
  } else if (OB_FAIL(deserialize(serialize_str.ptr(), serialize_len, pos))) {
  }
  return ret;
}

int ObTabletColumnChecksumMeta::get_str_obj(
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
    } else {
      obj.set_varbinary(str);
    }
  } else if (OB_FAIL(get_hex_str(allocator, str))) {
  } else {
    obj.set_varchar(str);
  }
  return ret;
}

int ObTabletColumnChecksumMeta::get_hex_str(
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
  } else if (OB_UNLIKELY(serialize_pos > serialize_size)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("serialize error", KR(ret), K(serialize_pos), K(serialize_size));
  } else if (OB_ISNULL(hex_buf = static_cast<char*>(allocator.alloc(hex_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", KR(ret), K(hex_size));
  } else if (OB_FAIL(hex_print(serialize_buf, serialize_pos, hex_buf, hex_size, hex_pos))) {
  } else if (OB_UNLIKELY(hex_pos > hex_size)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("encode error", KR(ret), K(hex_pos), K(hex_size));
  } else {
    column_meta_hex_str.assign_ptr(hex_buf, static_cast<int32_t>(hex_size));
  }
  return ret;
}

int ObTabletColumnChecksumMeta::get_serialize_str(
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
  } else if (OB_UNLIKELY(serialize_pos > serialize_size)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("serialize error", KR(ret), K(serialize_pos), K(serialize_size));
  } else {
    str.assign_ptr(serialize_buf, static_cast<int32_t>(serialize_size));
  }
  return ret;
}

/****************************** ObTabletLocalChecksumItem ******************************/

ObTabletLocalChecksumItem::ObTabletLocalChecksumItem()
  : tablet_id_(),
    row_count_(0),
    compaction_scn_(),
    data_checksum_(0),
    column_meta_(),
    data_checksum_type_(ObDataChecksumType::DATA_CHECKSUM_MAX)
{}

void ObTabletLocalChecksumItem::reset()
{
  tablet_id_.reset();
  row_count_ = 0;
  compaction_scn_.reset();
  data_checksum_ = 0;
  column_meta_.reset();
  data_checksum_type_ = ObDataChecksumType::DATA_CHECKSUM_MAX;
}

bool ObTabletLocalChecksumItem::is_key_valid() const
{
  return tablet_id_.is_valid();
}

bool ObTabletLocalChecksumItem::is_valid() const
{
  return is_key_valid()
       && compaction_scn_.is_valid()
       && column_meta_.is_valid()
       && is_valid_data_checksum_type(data_checksum_type_);
}

void ObTabletLocalChecksumItem::set_data_checksum_type()
{
  data_checksum_type_ = ObDataChecksumType::DATA_CHECKSUM_NORMAL_WITH_NORMAL_COLUMN;
}


int ObTabletLocalChecksumItem::assign(const ObTabletLocalChecksumItem &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    if (OB_FAIL(column_meta_.assign(other.column_meta_))) {
    } else {
      tablet_id_ = other.tablet_id_;
      row_count_ = other.row_count_;
      compaction_scn_ = other.compaction_scn_;
      data_checksum_ = other.data_checksum_;
      data_checksum_type_ = other.data_checksum_type_;
    }
  }
  return ret;
}

int ObTabletLocalChecksumItem::set_ckm_mem_attr()
{
  int ret = OB_SUCCESS;
  column_meta_.column_checksums_.set_attr(ObMemAttr("LocalCkm"));
  return ret;
}

/****************************** ObTabletLocalChecksumOperator ******************************/

int ObTabletLocalChecksumOperator::init(ObSQLiteConnectionPool *pool)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("SQLite connection pool is null", K(ret));
  } else if (OB_FAIL(storage_.init(pool))) {
  }
  return ret;
}

int ObTabletLocalChecksumOperator::batch_update_with_trans(
    ObSQLiteConnection *conn,
    const common::ObIArray<ObTabletLocalChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid connection", K(ret));
  } else if (OB_UNLIKELY(items.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), "items count", items.count());
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    // Use SQLite storage within the transaction
    // Note: The transaction is managed by the caller (ObTabletRuntimeMetaUpdater)
    const char *upsert_sql =
      "INSERT INTO __all_tablet_local_checksum "
      "(tablet_id, compaction_scn, "
      " row_count, data_checksum, column_checksums, b_column_checksums, "
      " data_checksum_type) "
      "VALUES (?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(tablet_id) DO UPDATE SET "
      "compaction_scn = excluded.compaction_scn, "
      "row_count = excluded.row_count, "
      "data_checksum = excluded.data_checksum, "
      "column_checksums = excluded.column_checksums, "
      "b_column_checksums = excluded.b_column_checksums, "
      "data_checksum_type = excluded.data_checksum_type;";

    ObSQLiteStmt *stmt = nullptr;
    if (OB_FAIL(conn->prepare_execute(upsert_sql, stmt))) {
    } else {
      common::ObArenaAllocator allocator;
      for (int64_t i = 0; OB_SUCC(ret) && i < items.count(); ++i) {
        const ObTabletLocalChecksumItem &item = items.at(i);
        // Convert column_meta to string
        common::ObString column_checksums_str;
        common::ObString b_column_checksums_str;
        if (OB_UNLIKELY(!item.is_valid())) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid local checksum item", K(ret), K(item));
        } else {
          if (OB_FAIL(get_visible_column_meta(item.column_meta_, allocator, column_checksums_str))) {
          } else {
            common::ObObj obj;
            if (OB_FAIL(item.column_meta_.get_str_obj(item.data_checksum_type_, allocator, obj, b_column_checksums_str))) {
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
            return OB_SUCCESS;
          };

          if (OB_FAIL(conn->step_execute(stmt, binder))) {
          }
        }
      }

      // Finalize statement (but don't commit/rollback - caller manages transaction)
      conn->finalize_execute(stmt);
    }
  }
  return ret;
}

int ObTabletLocalChecksumOperator::batch_remove_with_trans(
    ObSQLiteConnection *conn,
    const common::ObIArray<share::ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  const int64_t tablet_count = tablet_infos.count();
  if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid connection", K(ret));
  } else if (OB_UNLIKELY(tablet_count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_count));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    const char *delete_sql =
      "DELETE FROM __all_tablet_local_checksum "
      "WHERE tablet_id = ?;";

    ObSQLiteStmt *stmt = nullptr;
    if (OB_FAIL(conn->prepare_execute(delete_sql, stmt))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < tablet_count; ++i) {
        const ObTabletRuntimeInfo &tablet_info = tablet_infos.at(i);
        if (OB_UNLIKELY(!tablet_info.primary_keys_are_valid())) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid tablet runtime metadata key", K(ret), K(tablet_info));
        } else {
          auto binder = [&](ObSQLiteBinder &b) -> int {
            b.bind_int64(tablet_info.get_tablet_id().id());
            return OB_SUCCESS;
          };

          if (OB_FAIL(conn->step_execute(stmt, binder))) {
          }
        }
      }
      // Finalize statement (but don't commit/rollback - caller manages transaction)
      conn->finalize_execute(stmt);
    }
  }
  return ret;
}

int ObTabletLocalChecksumOperator::get_tablet_checksums(const ObIArray<compaction::ObTabletCheckInfo> &pairs,
    ObLocalTabletChecksumArray &tablet_checksum_items)
{
  int ret = OB_SUCCESS;
  const int64_t pairs_cnt = pairs.count();
  if (OB_UNLIKELY(pairs_cnt <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(pairs));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    ObSEArray<ObTabletID, 64> tablet_ids;
    for (int64_t i = 0; OB_SUCC(ret) && i < pairs_cnt; ++i) {
      const compaction::ObTabletCheckInfo &check_info = pairs.at(i);
      if (OB_FAIL(tablet_ids.push_back(check_info.get_tablet_id()))) {
      }
    }
    if (OB_SUCC(ret)) {
      ret = storage_.batch_get(tablet_ids, SCN(), tablet_checksum_items, false);
      if (OB_FAIL(ret)) {
      } else {
      }
    }
  }
  return ret;
}

int ObTabletLocalChecksumOperator::batch_get(
    const ObIArray<ObTabletID> &tablet_ids,
    const SCN &compaction_scn,
    ObLocalTabletChecksumArray &items,
    const bool include_larger_than)
{
  int ret = OB_SUCCESS;
  items.reset();
  const int64_t tablet_count = tablet_ids.count();
  if (OB_UNLIKELY(tablet_count < 1 || !compaction_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_count), K(compaction_scn));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_count; ++i) {
      if (OB_UNLIKELY(!tablet_ids.at(i).is_valid())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid tablet id", KR(ret), K(tablet_ids.at(i)));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(storage_.batch_get(tablet_ids, compaction_scn, items, include_larger_than))) {
      LOG_WARN("failed to batch get from storage", K(ret));
    }
  }
  return ret;
}

int ObTabletLocalChecksumOperator::get_local_tablet_checksum_items(
    const SCN &compaction_scn,
    const ObIArray<ObTabletID> &tablet_ids,
    ObLocalTabletChecksumArray &items)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(batch_get(tablet_ids, compaction_scn,
        items, false/*include_larger_than*/))) {
  } else if (items.get_tablet_cnt() < tablet_ids.count()) {
    ret = OB_ITEM_NOT_MATCH;
    LOG_WARN("fail to get local tablet checksum items", KR(ret), K(compaction_scn),
      K(items));
  }
  return ret;
}

int ObTabletLocalChecksumOperator::recover_mock_column_meta(
    ObTabletColumnChecksumMeta &column_meta)
{
  for (int64_t i = 0; i < column_meta.column_checksums_.count(); ++i) {
    column_meta.column_checksums_[i] -= MOCK_COLUMN_CHECKSUM;
  }
  return OB_SUCCESS;
}

int ObTabletLocalChecksumOperator::get_visible_column_meta(
    const ObTabletColumnChecksumMeta &column_meta,
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

} // share
} // oceanbase
