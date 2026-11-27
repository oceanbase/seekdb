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

#include <cstring>
#include "ob_micro_block_header.h"
#include "rust/micro_block.h"

namespace oceanbase
{
namespace blocksstable
{
namespace
{
int record_error(const micro_block::Code code)
{
  switch (code) {
    case micro_block::Code::Ok: return OB_SUCCESS;
    case micro_block::Code::InvalidArgument: return OB_INVALID_ARGUMENT;
    case micro_block::Code::InvalidData: return OB_INVALID_DATA;
    case micro_block::Code::ChecksumMismatch: return OB_PHYSIC_CHECKSUM_ERROR;
    case micro_block::Code::Unsupported: return OB_NOT_SUPPORTED;
    case micro_block::Code::OutputTooSmall: return OB_BUF_NOT_ENOUGH;
    case micro_block::Code::SizeOverflow: return OB_SIZE_OVERFLOW;
    default: return OB_ERR_UNEXPECTED;
  }
}

rust::Slice<const uint8_t> bytes(const char *buf, const int64_t size)
{
  return {reinterpret_cast<const uint8_t *>(buf), static_cast<size_t>(size)};
}

void from_rust(const micro_block::RecordMetadata &meta, ObMicroBlockHeader &header)
{
  header.header_size_ = meta.header_size;
  header.column_count_ = meta.column_count;
  header.rowkey_column_count_ = meta.rowkey_column_count;
  header.row_count_ = meta.row_count;
  header.row_store_type_ = meta.row_store_type;
  header.original_length_ = meta.original_length;
  header.max_merged_trans_version_ = meta.max_merged_trans_version;
  header.data_length_ = meta.data_length;
  header.data_zlength_ = meta.data_zlength;
  header.data_checksum_ = meta.data_checksum;
  header.has_column_checksum_ = meta.has_column_checksum;
  header.has_string_out_row_ = meta.has_string_out_row;
  header.all_lob_in_row_ = meta.all_lob_in_row;
  header.contains_hash_index_ = meta.contains_hash_index;
  header.has_min_merged_trans_version_ = meta.has_min_merged_trans_version;
  header.min_merged_trans_version_ = meta.min_merged_trans_version;
  header.single_version_rows_ = meta.single_version_rows;
  header.contain_uncommitted_rows_ = meta.contain_uncommitted_rows;
  header.is_last_row_last_flag_ = meta.is_last_row_last_flag;
}
} // namespace

uint32_t ObMicroBlockHeader::get_serialize_size(const int64_t column_count, const bool has_column_checksum)
{
  return column_count < 0 || column_count > UINT16_MAX
      ? 0 : micro_block::record_header_size(column_count, has_column_checksum);
}

int ObMicroBlockHeader::read(const char *buf, const int64_t buf_len,
    const ReadMode mode)
{
  int ret = OB_SUCCESS;
  if (nullptr == buf || buf_len < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const auto input = bytes(buf, buf_len);
    micro_block::RecordResult result{};
    switch (mode) {
      case ReadMode::HEADER: result = micro_block::read_record_header(input); break;
      case ReadMode::CHECKED_HEADER: result = micro_block::check_record_header(input); break;
      case ReadMode::STORED_RECORD: result = micro_block::check_stored_record(input); break;
      case ReadMode::EXPANDED: result = micro_block::read_expanded_record_header(input); break;
      case ReadMode::STORED_PREFIX: result = micro_block::check_stored_record_prefix(input); break;
    }
    if (OB_FAIL(record_error(result.code))) {
    } else {
      from_rust(result.metadata, *this);
      prefix_ = buf;
      prefix_size_ = header_size_;
      column_checksums_ = nullptr;
      rewrite_column_checksums_ = false;
    }
  }
  return ret;
}

int ObMicroBlockHeader::deserialize(const char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (nullptr == buf || pos < 0 || pos > buf_len) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(read(buf + pos, buf_len - pos, ReadMode::HEADER))) {
  } else {
    pos += header_size_;
  }
  return ret;
}

int ObMicroBlockHeader::deserialize_expanded_record(const char *buf, const int64_t size)
{
  return read(buf, size, ReadMode::EXPANDED);
}

int ObMicroBlockHeader::deserialize_and_check_header(const char *buf, const int64_t size)
{
  return read(buf, size, ReadMode::CHECKED_HEADER);
}

int ObMicroBlockHeader::deserialize_and_check_record(const char *buf, const int64_t size)
{
  return read(buf, size, ReadMode::STORED_RECORD);
}

int ObMicroBlockHeader::deserialize_and_check_record_prefix(const char *buf, const int64_t size)
{
  return read(buf, size, ReadMode::STORED_PREFIX);
}

void ObMicroBlockHeader::set_column_checksums(const uint16_t column_count, const int64_t *checksums)
{
  column_count_ = column_count;
  column_checksums_ = checksums;
  has_column_checksum_ = checksums != nullptr;
  header_size_ = get_serialize_size(column_count_, has_column_checksum_);
  rewrite_column_checksums_ = true;
}

int ObMicroBlockHeader::accumulate_column_checksums(int64_t *sums) const
{
  int ret = OB_SUCCESS;
  if (nullptr == sums || !has_column_checksum_) {
    ret = OB_INVALID_ARGUMENT;
  } else if (nullptr != column_checksums_) {
    for (uint16_t i = 0; i < column_count_; ++i) {
      sums[i] = static_cast<int64_t>(static_cast<uint64_t>(sums[i]) + column_checksums_[i]);
    }
  } else {
    ret = record_error(micro_block::accumulate_record_column_checksums(
        bytes(prefix_, prefix_size_), rust::Slice<int64_t>(sums, column_count_)));
  }
  return ret;
}

int ObMicroBlockHeader::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (nullptr == buf || nullptr == prefix_ || pos < 0 || pos > buf_len || !is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (header_size_ > buf_len - pos) {
    ret = OB_BUF_NOT_ENOUGH;
  } else if (rewrite_column_checksums_) {
    const auto *checksums = reinterpret_cast<const uint64_t *>(column_checksums_);
    ret = record_error(micro_block::rewrite_record_column_checksums(
        rust::Slice<uint8_t>(reinterpret_cast<uint8_t *>(buf + pos), header_size_),
        bytes(prefix_, prefix_size_), column_count_, has_column_checksum_,
        rust::Slice<const uint64_t>(checksums, has_column_checksum_ ? column_count_ : 0)));
  } else {
    std::memmove(buf + pos, prefix_, prefix_size_);
  }
  if (OB_SUCC(ret)) {
    pos += header_size_;
  }
  return ret;
}

int ObMicroBlockHeader::seal(char *prefix, const char *payload, const int64_t size)
{
  int ret = OB_SUCCESS;
  if (prefix != prefix_ || nullptr == prefix || nullptr == payload || size < 0
      || rewrite_column_checksums_) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const auto result = micro_block::reseal_record_header_in_place(
        rust::Slice<uint8_t>(reinterpret_cast<uint8_t *>(prefix), prefix_size_),
        bytes(payload, size));
    if (OB_FAIL(record_error(result.code))) {
    } else {
      from_rust(result.metadata, *this);
    }
  }
  return ret;
}

} // namespace blocksstable
} // namespace oceanbase
