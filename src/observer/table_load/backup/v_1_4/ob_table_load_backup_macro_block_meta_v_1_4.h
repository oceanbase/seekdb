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

#pragma once
#include "storage/blocksstable/ob_row_reader.h"
#include "common/object/ob_object.h"
#include "lib/container/ob_array_serialization.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace observer
{

enum ObTableLoadBackupMacroBlockType_V_1_4
{
  Free = 0,
  SSTableData = 1,
  PartitionMeta = 2,
  // SchemaData = 3,
  // Compressor = 4,
  MacroMeta = 5,
  Reserved = 6,
  MacroBlockSecondIndex = 7,
  SortTempData = 8,
  LobData = 9,
  LobIndex = 10,
  MaxMacroType,
};

class ObTableLoadBackupMacroBlockMeta_V_1_4
{
public:
  ObTableLoadBackupMacroBlockMeta_V_1_4()
    : attr_(0),
      data_version_(0),
      column_number_(0),
      rowkey_column_number_(0),
      column_index_scale_(0),
      row_store_type_(0),
      row_count_(0),
      occupy_size_(0),
      data_checksum_(0),
      micro_block_count_(0),
      micro_block_data_offset_(0),
      micro_block_index_offset_(0),
      micro_block_endkey_offset_(0),
      compressor_(nullptr),
      column_id_array_(nullptr),
      column_type_array_(nullptr),
      column_checksum_(nullptr),
      endkey_(nullptr),
      table_id_(0),
      data_seq_(-1),
      schema_version_(0),
      write_seq_(0),
      create_timestamp_(0),
      retire_timestamp_(0),
      bf_build_timestamp_(0),
      empty_read_cnt_(nullptr),
      collation_free_endkey_(nullptr) {}
  ~ObTableLoadBackupMacroBlockMeta_V_1_4() {}
  int deserialize(const char *buf, int64_t data_len, int64_t &pos);
  TO_STRING_KV(K(attr_), K(data_version_), K(column_number_), K(rowkey_column_number_), 
               K(column_index_scale_), K(row_store_type_), K(row_count_), K(occupy_size_), 
               K(data_checksum_), K(micro_block_count_), K(micro_block_data_offset_), 
               K(micro_block_index_offset_), K(micro_block_endkey_offset_), K(OB_P(compressor_)), 
               K(ObArrayWrap<uint16_t>(column_id_array_, column_number_)),
               K(ObArrayWrap<ObObjMeta>(column_type_array_, column_number_)), 
               K(ObArrayWrap<int64_t>(column_checksum_, column_number_)), 
               K(ObRowkey(endkey_, rowkey_column_number_)), K(table_id_), K(data_seq_), K(schema_version_), 
               K(write_seq_), K(create_timestamp_), K(retire_timestamp_), K(bf_build_timestamp_), 
               K(ObArrayWrap<int64_t>(empty_read_cnt_, rowkey_column_number_)),
               K(ObRowkey(collation_free_endkey_, rowkey_column_number_)));
public:
  //For compatibility, the variables in this struct MUST NOT be deleted or moved.
  //You should ONLY add variables at the end.
  //Note that if you use complex structure as variables, the complex structure should also keep compatibility.

  //The following variables need to be serialized
  int16_t attr_;       // Low 8 bits 0, 1, 2, 3, 4, 5 represent idle, sstable data, tablet metadata, schema, compressor name, macro block metadata; other positions are 0;
  union
  {
    uint64_t data_version_;  // sstable macro block: major version number (high 48 bits) and minor version number (low 16 bits)
    int64_t previous_block_index_; // nonsstable: previous_block_index_ link.
  };
  int16_t column_number_;            // column count of this table (size of column_checksum_)
  int16_t rowkey_column_number_;     // rowkey column count of this table
  int16_t column_index_scale_;       // store column index scale percent of column count;
  int16_t row_store_type_;                     // reserve
  int32_t row_count_;                // row count of macro block;
  int32_t occupy_size_;              // data size of macro block;
  int64_t data_checksum_;            // data checksum of macro block
  int32_t micro_block_count_;        // micro block info in ObSSTableMacroBlockHeader
  int32_t micro_block_data_offset_;  // data offset base on macro block header.
  int32_t micro_block_index_offset_; // data_size = index_offset - data_offset
  int32_t micro_block_endkey_offset_; // index_size = endkey_offset - index_offset, endkey_size = occupy_size - endkey_offset
  char    *compressor_;
  uint16_t *column_id_array_;
  common::ObObjMeta *column_type_array_;
  int64_t *column_checksum_;
  common::ObObj   *endkey_;
  uint64_t table_id_;
  int64_t data_seq_;  // sequence in partition meta.
  int64_t schema_version_;

  //The following variables do not need to be serialized
  int32_t write_seq_;       // increment 1 every reuse pass.
  int64_t create_timestamp_;
  int64_t retire_timestamp_;
  int64_t bf_build_timestamp_;
  int64_t *empty_read_cnt_;
  common::ObObj *collation_free_endkey_;
};

} // namespace observer
} // namespace oceanbase
