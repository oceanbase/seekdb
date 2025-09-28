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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_table_partition_ranges.h"
#include "sql/engine/expr/ob_expr.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
namespace sql
{
using namespace common;

OB_DEF_SERIALIZE(ObPartitionScanRanges)
{
  int ret = OK_;
  UNF_UNUSED_SER;
  LST_DO_CODE(OB_UNIS_ENCODE, partition_id_);
  if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, ranges_.count()))) {
    LOG_WARN("fail to encode ranges count", K(ret), K(ranges_.count()));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < ranges_.count(); ++i) {
    if (OB_FAIL(ranges_.at(i).serialize(buf, buf_len, pos))) {
      LOG_WARN("fail to serialize key range", K(ret), K(i));
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObPartitionScanRanges)
{
  int ret = OK_;
  UNF_UNUSED_DES;
  LST_DO_CODE(OB_UNIS_DECODE, partition_id_);
  if (OB_SUCC(ret)) {
    int64_t count = 0;
    ranges_.reset();
    if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &count))) {
      LOG_WARN("fail to decode key ranges count", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < count; i ++) {
      if (OB_ISNULL(deserialize_allocator_)) {
        ret = OB_NOT_INIT;
        LOG_WARN("deserialize allocator is NULL", K(ret));
      } else {
        ObObj array[OB_MAX_ROWKEY_COLUMN_NUMBER * 2];
        ObNewRange copy_range;
        ObNewRange key_range;
        copy_range.start_key_.assign(array, OB_MAX_ROWKEY_COLUMN_NUMBER);
        copy_range.end_key_.assign(array + OB_MAX_ROWKEY_COLUMN_NUMBER, OB_MAX_ROWKEY_COLUMN_NUMBER);
        if (OB_FAIL(copy_range.deserialize(buf, data_len, pos))) {
          LOG_WARN("fail to deserialize range", K(ret));
        } else if (OB_FAIL(deep_copy_range(*deserialize_allocator_, copy_range, key_range))) {
          LOG_WARN("fail to deep copy range", K(ret));
        } else if (OB_FAIL(ranges_.push_back(key_range))) {
          LOG_WARN("fail to add key range to array", K(ret));
        }
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObPartitionScanRanges)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, partition_id_);
  len += serialization::encoded_length_vi64(ranges_.count());
  for (int64_t i = 0; i < ranges_.count(); ++i) {
    len += ranges_.at(i).get_serialize_size();
  }
  return len;
}

OB_DEF_SERIALIZE(ObMultiPartitionsRangesWarpper)
{
  int ret = OK_;
  UNF_UNUSED_SER;
  if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, partitions_ranges_.count()))) {
    LOG_WARN("fail to encode ranges count", K(ret), K(partitions_ranges_.count()));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < partitions_ranges_.count(); ++i) {
    if (OB_FAIL(partitions_ranges_.at(i)->serialize(buf, buf_len, pos))) {
      LOG_WARN("fail to serialize key range", K(ret), K(i));
    }
  }
  LST_DO_CODE(OB_UNIS_ENCODE, mode_);
  return ret;
}

OB_DEF_DESERIALIZE(ObMultiPartitionsRangesWarpper)
{
  int ret = OK_;
  UNF_UNUSED_DES;
  if (OB_SUCC(ret)) {
    int64_t count = 0;
    partitions_ranges_.reset();
    if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &count))) {
      LOG_WARN("fail to decode partition ranges count", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < count; i ++) {
      ObPartitionScanRanges *partition_ranges = nullptr;
      if (OB_FAIL(get_new_partition_ranges(partition_ranges))) {
        LOG_WARN("Failed to get new partition ranges", K(ret));
      } else {
        partition_ranges->set_des_allocator(&allocator_);
        if (OB_FAIL(partition_ranges->deserialize(buf, data_len, pos))) {
          LOG_WARN("fail to deserialize range", K(ret));
        }
      }
    }
  }
  LST_DO_CODE(OB_UNIS_DECODE, mode_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObMultiPartitionsRangesWarpper)
{
  int64_t len = 0;
  len += serialization::encoded_length_vi64(partitions_ranges_.count());
  for (int64_t i = 0; i < partitions_ranges_.count(); ++i) {
    len += partitions_ranges_.at(i)->get_serialize_size();
  }
  LST_DO_CODE(OB_UNIS_ADD_LEN, mode_);
  return len;
}



int ObMultiPartitionsRangesWarpper::get_partition_ranges_by_partition_id(int64_t partition_id, ObPartitionScanRanges *&partition_ranges)
{
  int ret = OB_SUCCESS;
  partition_ranges = nullptr;
  ARRAY_FOREACH_X(partitions_ranges_, idx, cnt, OB_SUCC(ret)) {
    ObPartitionScanRanges *tmp_partition_ranges = partitions_ranges_.at(idx);
    if (nullptr == tmp_partition_ranges) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("The partition ranges is null", K(ret));
    } else if (partition_id == tmp_partition_ranges->partition_id_) {
      partition_ranges = tmp_partition_ranges;
    }
  }
  if (nullptr == partition_ranges) {
    ret = OB_ENTRY_NOT_EXIST;
  }
  return ret;
}

int ObMultiPartitionsRangesWarpper::get_new_partition_ranges(ObPartitionScanRanges *&partition_ranges)
{
  int ret = OB_SUCCESS;
  void *buf = allocator_.alloc(sizeof(ObPartitionScanRanges));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("No memory", K(ret));
  } else if (FALSE_IT(partition_ranges = new (buf) ObPartitionScanRanges())) {
  } else if (OB_FAIL(partitions_ranges_.push_back(partition_ranges))) {
    LOG_WARN("Failed to push back partition ranges", K(ret));
    partition_ranges = nullptr;
  }
  return ret;
}



int ObMultiPartitionsRangesWarpper::init_main_table_rowkey(const int64_t column_count, common::ObNewRow &row)
{
  int ret = OB_SUCCESS;
  void *ptr = NULL;
  if (column_count <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(column_count));
  } else if (OB_ISNULL(ptr = allocator_.alloc(column_count * sizeof(ObObj)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("alloc memory for row failed", "size", column_count * sizeof(ObObj));
  } else {
    row.cells_ = new(ptr) common::ObObj[column_count];
    row.count_ = column_count;
  }
  return ret;
}




}
}



