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
#include "lib/checksum/ob_checksum_utils.h"
#include "ob_block_sstable_struct.h"
#include <inttypes.h>
#include "share/ob_server_struct.h"

using namespace oceanbase;
using namespace common;

namespace oceanbase
{
using namespace share;
using namespace share::schema;
using namespace storage;
namespace blocksstable
{

const char *BLOCK_SSTBALE_DIR_NAME = "sstable";
const char *BLOCK_SSTBALE_FILE_NAME = "block_file";

const bool ObMicroBlockEncoderOpt::ENCODINGS_DEFAULT[ObColumnHeader::MAX_TYPE] = {true, true, true, true, true, true, true, true, true, true};
const bool ObMicroBlockEncoderOpt::ENCODINGS_NONE[ObColumnHeader::MAX_TYPE] = {false, false, false, false, false, false, false, false, false, false};
const bool ObMicroBlockEncoderOpt::ENCODINGS_FOR_PERFORMANCE[ObColumnHeader::MAX_TYPE] = {true, true, false, true, false, false, false, false, false, false};

//================================ObStorageEnv======================================

ObMicroBlockId::ObMicroBlockId()
  : macro_id_(), offset_(0), size_(0)
{
}

ObMicroBlockId::ObMicroBlockId(
    const MacroBlockId &macro_id,
    const int64_t offset,
    const int64_t size)
    : macro_id_(macro_id),
      offset_(offset),
      size_(size)
{
}

bool ObMicroBlockEncodingCtx::is_valid() const
{
  return macro_block_size_ > 0 && micro_block_size_ > 0 && rowkey_column_cnt_ >= 0
      && column_cnt_ >= rowkey_column_cnt_ && NULL != col_descs_
      && (FLAT_ROW_STORE != row_store_type_ &&  MAX_ROW_STORE != row_store_type_)
      && compressor_type_ != ObCompressorType::INVALID_COMPRESSOR
      && compressor_type_ != ObCompressorType::MAX_COMPRESSOR;
}

ObMacroBlockSchemaInfo::ObMacroBlockSchemaInfo()
  : column_number_(0), rowkey_column_number_(0), schema_version_(0), schema_rowkey_col_cnt_(0), compressor_(nullptr),
    column_id_array_(nullptr), column_type_array_(nullptr), column_order_array_(nullptr)
{
}





int64_t ObMacroBlockSchemaInfo::get_deep_copy_size() const
{
  int64_t deep_copy_size = 0;
  deep_copy_size = sizeof(ObMacroBlockSchemaInfo);
  if (nullptr != compressor_) {
    deep_copy_size += strlen(compressor_) + 1;
  }
  if (column_number_ > 0) {
    const int64_t column_id_size = column_number_ * sizeof(uint16_t);
    const int64_t column_type_size = column_number_ * sizeof(ObObjMeta);
    const int64_t column_order_size = column_number_ * sizeof(ObOrderType);
    const int64_t total_array_size = column_id_size + column_type_size + column_order_size;
    deep_copy_size += total_array_size;
  }
  return deep_copy_size;
}


int64_t ObMacroBlockSchemaInfo::to_string(char *buf, const int64_t buf_len) const
{
 int64_t pos = 0;
  J_KV(
      K_(column_number),
      K_(rowkey_column_number),
      K_(schema_version),
      K_(schema_rowkey_col_cnt),
      K_(compressor)
  );
  J_COMMA();
  if (NULL != column_id_array_ && column_number_ > 0) {
    J_KV("column_id_array", ObArrayWrap<uint16_t>(column_id_array_, column_number_));
  }
  if (NULL != column_type_array_ && column_number_ > 0) {
    J_KV("column_type_array", ObArrayWrap<ObObjMeta>(column_type_array_, column_number_));
  }
  if (NULL != column_order_array_ && column_number_ > 0) {
    J_KV("column_order_array", ObArrayWrap<ObOrderType>(column_order_array_, column_number_));
  }
  return pos;
}


ObSSTableColumnMeta::ObSSTableColumnMeta()
  : column_id_(0),
    column_default_checksum_(0),
    column_checksum_(0)
{
}


ObSSTableColumnMeta::~ObSSTableColumnMeta()
{
}

bool ObSSTableColumnMeta::operator==(const ObSSTableColumnMeta &other) const
{
  return column_id_ == other.column_id_
        && column_default_checksum_ == other.column_default_checksum_
        && column_checksum_ == other.column_checksum_;
}



OB_SERIALIZE_MEMBER(ObSSTableColumnMeta,
    column_id_,
    column_default_checksum_,
    column_checksum_);



OB_SERIALIZE_MEMBER(ObSSTablePair, data_version_, data_seq_);

ObSimpleMacroBlockInfo::ObSimpleMacroBlockInfo()
  : macro_id_(),
    last_access_time_(INT64_MAX),
    ref_cnt_(0)
{
}

void ObSimpleMacroBlockInfo::reset()
{
  macro_id_.reset();
  last_access_time_ = INT64_MAX;
  ref_cnt_ = 0;
}

ObMacroBlockMarkerStatus::ObMacroBlockMarkerStatus()
  : total_block_count_(0),
    reserved_block_count_(0),
    linked_block_count_(0),
    tmp_file_count_(0),
    data_block_count_(0),
    index_block_count_(0),
    ids_block_count_(0),
    disk_block_count_(0),
    hold_count_(0),
    pending_free_count_(0),
    free_count_(0),
    mark_cost_time_(0),
    sweep_cost_time_(0),
    start_time_(0),
    last_end_time_(0),
    hold_info_(),
    mark_finished_(false)
{
}

void ObMacroBlockMarkerStatus::reset()
{
  total_block_count_ = 0;
  reuse();
}

void ObMacroBlockMarkerStatus::fill_comment(char *buf, const int32_t buf_len) const
{
  const int64_t now = ObTimeUtility::current_time();
  if (NULL != buf && buf_len > 0) {
    if (!hold_info_.macro_id_.is_valid()) {
      buf[0] = '\0';
    } else {
      int64_t pos = 0;
      J_NAME("the earliest hold macro block info");
      J_COLON();
      BUF_PRINTO(hold_info_);
      J_COMMA();
      BUF_PRINTF("last access interval = %" PRId64 "s", (now - hold_info_.last_access_time_) / 1000 / 1000);
    }
  }
}

void ObMacroBlockMarkerStatus::reuse()
{
  reserved_block_count_ = 0;
  linked_block_count_ = 0;
  tmp_file_count_ = 0;
  data_block_count_ = 0;
  index_block_count_ = 0;
  ids_block_count_ = 0;
  disk_block_count_ = 0;
  hold_count_ = 0;
  pending_free_count_ = 0;
  free_count_ = 0;
  mark_cost_time_ = 0;
  sweep_cost_time_ = 0;
  start_time_ = 0;
  last_end_time_ = 0;
  hold_info_.reset();
  mark_finished_ = false;
}

constexpr uint8_t ObColClusterInfoMask::BYTES_TYPE_TO_LEN[];

}
}
