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

#include "utils.h"
#include "lib/ob_define.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/oblog/ob_log_level.h"
#include "share/backup/ob_backup_io_adapter.h"

namespace oceanbase
{

using namespace share;
using namespace common;

namespace tools
{

DEFINE_DESERIALIZE(ObArchiveFileHeader)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || 0 > data_len) {
    ret = OB_INVALID_DATA;
    OB_LOG(WARN, "invalid arguments", KP(buf), K(data_len), K(ret));
  } else if (OB_FAIL(serialization::decode_i16(buf, data_len, pos, &magic_))) {
    OB_LOG(WARN, "failed to decode magic_", KP(buf), K(data_len), K(pos), K(ret));
  } else if (OB_FAIL(serialization::decode_i16(buf, data_len, pos, &version_))) {
    OB_LOG(WARN, "failed to decode version_", KP(buf), K(data_len), K(pos), K(ret));
  } else if (OB_FAIL(serialization::decode_i32(buf, data_len, pos, &flag_))) {
    OB_LOG(WARN, "failed to decode flag_", KP(buf), K(data_len), K(pos), K(ret));
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, pos, &unit_size_))) {
    OB_LOG(WARN, "failed to decode unit_size_", KP(buf), K(data_len), K(pos), K(ret));
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, pos, &start_lsn_))) {
    OB_LOG(WARN, "failed to decode start_lsn_", KP(buf), K(data_len), K(pos), K(ret));
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, pos, &checksum_))) {
    OB_LOG(WARN, "failed to decode checksum_", KP(buf), K(data_len), K(pos), K(ret));
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObArchiveFileHeader)
{
  int64_t size = 0;
  size += serialization::encoded_length_i16(magic_);
  size += serialization::encoded_length_i16(version_);
  size += serialization::encoded_length_i32(flag_);
  size += serialization::encoded_length_i64(unit_size_);
  size += serialization::encoded_length_i64(start_lsn_);
  size += serialization::encoded_length_i64(checksum_);
  return size;
}

bool ObArchiveFileHeader::is_valid() const
{
  return ARCHIVE_FILE_HEADER_MAGIC == magic_
    && checksum_ == ob_crc64(this, sizeof(*this) - sizeof(checksum_));
}

int ObArchiveFileHeader::generate_header(const palf::LSN &lsn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(! lsn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    magic_ = ARCHIVE_FILE_HEADER_MAGIC;
    version_ = 1;
    flag_ = 0;
    unit_size_ = ObArchiveFileHeader::DEFAULT_ARCHIVE_UNIT_SIZE;
    start_lsn_ = lsn.val_;
    checksum_ = static_cast<int64_t>(ob_crc64(this, sizeof(*this) - sizeof(checksum_)));
  }
  return ret;
}

class ObFileRangeOp : public ObBaseDirEntryOperator
{
public:
  ObFileRangeOp() : min_file_id_(0), max_file_id_(0), file_num_(0) {}
  virtual ~ObFileRangeOp() {}
  int func(const dirent *entry) override;
  void get_file_id(int64_t &min_file_id, int64_t &max_file_id) {min_file_id = min_file_id_; max_file_id = max_file_id_;}
  int get_file_num() {return file_num_;}
private:
  int64_t min_file_id_;
  int64_t max_file_id_;
  int file_num_;
};

int ObFileRangeOp::func(const dirent *entry)
{
  int ret = OB_SUCCESS;
  int64_t tmp_file_id = 0;
  bool file_match = false;

  if (OB_ISNULL(entry)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid list entry, entry is null", K(ret), K(entry));
  } else if (OB_ISNULL(entry->d_name)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid list entry, d_name is null", K(ret), K(entry->d_name));
  } else {
    ObString file_name(entry->d_name);
    OB_LOG(TRACE, "valid entry ", K(entry->d_name));
    if (OB_FAIL(ObArchiveFileUtils::extract_file_id(file_name, tmp_file_id, file_match))) {
      OB_LOG(WARN, "extract_file_id fail", K(ret), K(file_name), K(tmp_file_id));
    } else if (OB_UNLIKELY(! file_match)) {
      // file_name not match, skip
    } else {
      OB_LOG(TRACE, "extract_file_id succ", K(file_name), K(tmp_file_id));
      min_file_id_ = (0 == min_file_id_) ? tmp_file_id : std::min(min_file_id_, tmp_file_id);
      max_file_id_ = std::max(max_file_id_, tmp_file_id);
      file_num_++;
    }
  }
  return ret;
}

int ObArchiveFileUtils::get_file_range(const ObString &prefix,
    const share::ObBackupStorageInfo *storage_info,
    int64_t &min_file_id,
    int64_t &max_file_id)
{
  int ret = OB_SUCCESS;
  min_file_id = 0;
  max_file_id = 0;
  ObBackupIoAdapter util;
  ObFileRangeOp file_range_op;

  if (OB_FAIL(util.adaptively_list_files(prefix, storage_info, file_range_op))) {
    OB_LOG(WARN, "list_files fail", K(ret), K(prefix));
  } else if (0 == file_range_op.get_file_num()) {
    ret = OB_ENTRY_NOT_EXIST;
    OB_LOG(TRACE, "no files", K(prefix));
  } else {
    file_range_op.get_file_id(min_file_id, max_file_id);
  }

  return ret; 
}

int ObArchiveFileUtils::extract_file_id(const ObString &file_name, int64_t &file_id, bool &match)
{
  int ret = OB_SUCCESS;
  int64_t tmp_file_id = 0;
  int32_t len = file_name.length() - strlen(OB_ARCHIVE_SUFFIX);
  char pure_name[OB_MAX_BACKUP_DEST_LENGTH] = { 0 };
  match = true;
  file_id = 0;

  if (OB_UNLIKELY(file_name.empty())) {
    match = false;
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", K(ret), K(file_name));
  } else if (OB_UNLIKELY(len <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "file name without a unified suffix", K(file_name), K(OB_ARCHIVE_SUFFIX));
  } else if (OB_FAIL(databuff_printf(pure_name, sizeof(pure_name), "%.*s", len, file_name.ptr()))) {
    OB_LOG(WARN, "databuff_printf failed", K(file_name), K(len));
  } else if (OB_FAIL(ob_atoll(pure_name, tmp_file_id))) {
    OB_LOG(WARN, "ignore invalid file name", K(file_name), K(pure_name));
    ret = OB_SUCCESS;
    match = false;
  } else {
    file_id = tmp_file_id;
    match = true;
  }

  return ret;
}

} // end namespace tools
} // end namespace oceanbase