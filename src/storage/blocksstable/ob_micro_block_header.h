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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_MICRO_BLOCK_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_MICRO_BLOCK_H_
#include "stdint.h"
#include  "lib/utility/ob_print_utils.h"
#include "storage/ob_i_store.h"
namespace oceanbase
{
using namespace common;
using namespace storage;
namespace blocksstable
{
struct ObMicroBlockHeader
{
  uint32_t header_size_ = 0;
  uint16_t column_count_ = 0;
  uint16_t rowkey_column_count_ = 0;
  bool has_column_checksum_ = false;
  bool has_string_out_row_ = false;
  bool all_lob_in_row_ = false;
  bool contains_hash_index_ = false;
  bool has_min_merged_trans_version_ = false;
  uint32_t row_count_ = 0;
  uint8_t row_store_type_ = common::MAX_ROW_STORE;
  bool single_version_rows_ = false;
  bool contain_uncommitted_rows_ = false;
  bool is_last_row_last_flag_ = false;
  int32_t original_length_ = 0;
  int64_t max_merged_trans_version_ = 0;
  int64_t min_merged_trans_version_ = 0;
  uint32_t data_length_ = 0;
  uint32_t data_zlength_ = 0;
  uint64_t data_checksum_ = 0;

  void reset() { *this = ObMicroBlockHeader(); }
  bool is_valid() const { return header_size_ != 0; }
  int accumulate_column_checksums(int64_t *sums) const;
  void set_column_checksums(const uint16_t column_count, const int64_t *checksums);
  int deserialize_and_check_record(const char *ptr, const int64_t size);
  int deserialize_and_check_record_prefix(const char *ptr, const int64_t size);
  int deserialize_expanded_record(const char *ptr, const int64_t size);
  int deserialize_and_check_header(const char *ptr, const int64_t size);
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t buf_len, int64_t &pos);
  int seal(char *prefix, const char *payload, const int64_t size);
  int64_t get_stored_size() const { return static_cast<int64_t>(header_size_) + data_zlength_; }
  uint32_t get_serialize_size() const { return header_size_; }
  static uint32_t get_serialize_size(const int64_t column_count, const bool has_column_checksum);

  TO_STRING_KV(K_(header_size), K_(column_count), K_(rowkey_column_count),
      K_(has_column_checksum), K_(row_count), K_(row_store_type),
      K_(original_length), K_(max_merged_trans_version), K_(data_length), K_(data_zlength), K_(data_checksum),
      K_(single_version_rows), K_(contain_uncommitted_rows), K_(is_last_row_last_flag), K(is_valid()));

  bool is_compressed_data() const { return data_length_ != data_zlength_; }
  bool contain_uncommitted_rows() const { return contain_uncommitted_rows_; }
  OB_INLINE bool has_string_out_row() const { return has_string_out_row_; }
  OB_INLINE bool has_lob_out_row() const { return !all_lob_in_row_; }
  bool is_last_row_last_flag() const { return is_last_row_last_flag_; }
  bool is_contain_hash_index() const { return contains_hash_index_; }
  bool is_trans_version_column_idx(const int64_t col_idx) const
  { return  rowkey_column_count_ > 0 && col_idx == rowkey_column_count_ - ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt(); }
private:
  enum class ReadMode { HEADER, CHECKED_HEADER, STORED_RECORD, STORED_PREFIX, EXPANDED };
  int read(const char *buf, const int64_t buf_len, const ReadMode mode);

  const char *prefix_ = nullptr;
  uint32_t prefix_size_ = 0;
  const int64_t *column_checksums_ = nullptr;
  bool rewrite_column_checksums_ = false;
};
}//end namespace blocksstable
}//end namespace oceanbase
#endif
