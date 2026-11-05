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

#include "storage/direct_load/ob_direct_load_sstable_data_block.h"

namespace oceanbase
{
namespace storage
{
using namespace common;

/**
 * ObDirectLoadSSTableDataBlock
 */

ObDirectLoadSSTableDataBlock::Header::Header()
  : last_row_pos_(0), reserved_(0)
{
}

ObDirectLoadSSTableDataBlock::Header::~Header()
{
}

void ObDirectLoadSSTableDataBlock::Header::reset()
{
  ObDirectLoadDataBlock::Header::reset();
  last_row_pos_ = 0;
  reserved_ = 0;
}

OB_DEF_SERIALIZE_SIMPLE(ObDirectLoadSSTableDataBlock::Header)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDirectLoadDataBlock::Header::serialize(buf, buf_len, pos))) {
    LOG_WARN("fail to encode header", KR(ret), K(buf_len), K(pos));
  } else if (OB_FAIL(NS_::encode_i32(buf, buf_len, pos, last_row_pos_))) {
    LOG_WARN("fail to encode i32", KR(ret), K(buf_len), K(pos), K(last_row_pos_));
  } else if (OB_FAIL(NS_::encode_i32(buf, buf_len, pos, reserved_))) {
    LOG_WARN("fail to encode i32", KR(ret), K(buf_len), K(pos), K(reserved_));
  }
  return ret;
}

OB_DEF_DESERIALIZE_SIMPLE(ObDirectLoadSSTableDataBlock::Header)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDirectLoadDataBlock::Header::deserialize(buf, data_len, pos))) {
    LOG_WARN("fail to decode header", KR(ret), K(data_len), K(pos));
  } else if (OB_FAIL(NS_::decode_i32(buf, data_len, pos, &last_row_pos_))) {
    LOG_WARN("fail to decode i32", KR(ret), K(data_len), K(pos), K(last_row_pos_));
  } else if (OB_FAIL(NS_::decode_i32(buf, data_len, pos, &reserved_))) {
    LOG_WARN("fail to decode i32", KR(ret), K(data_len), K(pos), K(reserved_));
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE_SIMPLE(ObDirectLoadSSTableDataBlock::Header)
{
  int64_t len = 0;
  len += ObDirectLoadDataBlock::Header::get_serialize_size();
  len += NS_::encoded_length_i32(last_row_pos_);
  len += NS_::encoded_length_i32(reserved_);
  return len;
}

/**
 * ObDirectLoadSSTableDataBlock
 */


/**
 * ObDirectLoadSSTableDataBlockDesc
 */

ObDirectLoadSSTableDataBlockDesc::ObDirectLoadSSTableDataBlockDesc()
  : fragment_idx_(0),
    offset_(0),
    size_(0),
    block_count_(0),
    is_left_border_(false),
    is_right_border_(false)
{
}

ObDirectLoadSSTableDataBlockDesc::~ObDirectLoadSSTableDataBlockDesc()
{
}


} // namespace storage
} // namespace oceanbase
