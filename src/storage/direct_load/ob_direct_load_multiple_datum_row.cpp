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

#include "storage/direct_load/ob_direct_load_multiple_datum_row.h"
#include "observer/table_load/ob_table_load_stat.h"
#include "storage/direct_load/ob_direct_load_datum_row.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
using namespace blocksstable;
using namespace table;

ObDirectLoadMultipleDatumRow::ObDirectLoadMultipleDatumRow()
  : allocator_("TLD_MDatumRow"), is_delete_(false), is_ack_(false), buf_size_(0), buf_(nullptr)
{
  allocator_.set_tenant_id(MTL_ID());
}

ObDirectLoadMultipleDatumRow::~ObDirectLoadMultipleDatumRow()
{
}

void ObDirectLoadMultipleDatumRow::reset()
{
  rowkey_.reset();
  seq_no_.reset();
  is_delete_ = false;
  is_ack_ = false;
  buf_size_ = 0;
  buf_ = nullptr;
  allocator_.reset();
}

void ObDirectLoadMultipleDatumRow::reuse()
{
  rowkey_.reuse();
  seq_no_.reset();
  is_delete_ = false;
  is_ack_ = false;
  buf_size_ = 0;
  buf_ = nullptr;
  allocator_.reuse();
}

int64_t ObDirectLoadMultipleDatumRow::get_deep_copy_size() const
{
  int64_t size = 0;
  if (OB_LIKELY(is_valid())) {
    size += rowkey_.get_deep_copy_size();
    size += buf_size_;
  }
  return size;
}


int ObDirectLoadMultipleDatumRow::from_datum_row(const ObTabletID &tablet_id,
                                                 const ObDirectLoadDatumRow &datum_row,
                                                 int64_t rowkey_column_count)
{
  OB_TABLE_LOAD_STATISTICS_TIME_COST(DEBUG, transfer_external_row_time_us);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() || !datum_row.is_valid() || rowkey_column_count <= 0 ||
                  datum_row.get_column_count() < rowkey_column_count)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(tablet_id), K(datum_row), K(rowkey_column_count));
  } else {
    reuse();
    seq_no_ = datum_row.seq_no_;
    is_delete_ = datum_row.is_delete_;
    is_ack_ = datum_row.is_ack_;
    if (OB_FAIL(rowkey_.assign(tablet_id, datum_row.storage_datums_, rowkey_column_count))) {
      LOG_WARN("fail to assign rowkey", KR(ret));
    } else if (datum_row.count_ > rowkey_column_count) {
      ObDirectLoadDatumArray serialize_datum_array;
      if (OB_FAIL(serialize_datum_array.assign(datum_row.storage_datums_ + rowkey_column_count,
                                               datum_row.count_ - rowkey_column_count))) {
        LOG_WARN("fail to assign datum array", KR(ret));
      } else {
        const int64_t buf_size = serialize_datum_array.get_serialize_size();
        char *buf = nullptr;
        int64_t pos = 0;
        if (OB_ISNULL(buf = static_cast<char *>(allocator_.alloc(buf_size)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc memory", KR(ret), K(buf_size));
        } else if (OB_FAIL(serialize_datum_array.serialize(buf, buf_size, pos))) {
          LOG_WARN("fail to serialize datum array", KR(ret));
        } else {
          buf_size_ = buf_size;
          buf_ = buf;
        }
      }
    }
  }
  return ret;
}

int ObDirectLoadMultipleDatumRow::to_datum_row(ObDirectLoadDatumRow &datum_row) const
{
  OB_TABLE_LOAD_STATISTICS_TIME_COST(DEBUG, transfer_datum_row_time_us);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected invalid row", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(buf_size_ > 0
                           ? datum_row.get_column_count() <= rowkey_.datum_array_.count_
                           : datum_row.get_column_count() != rowkey_.datum_array_.count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(datum_row), K(rowkey_.datum_array_.count_));
  } else {
    datum_row.seq_no_ = seq_no_;
    datum_row.is_delete_ = is_delete_;
    datum_row.is_ack_ = is_ack_;
    // from rowkey datum array
    for (int64_t i = 0; i < rowkey_.datum_array_.count_; ++i) {
      datum_row.storage_datums_[i] = rowkey_.datum_array_.datums_[i];
    }
    // from deserialize datum array
    if (buf_size_ > 0) {
      ObDirectLoadDatumArray deserialize_datum_array;
      int64_t pos = 0;
      if (OB_FAIL(
            deserialize_datum_array.assign(datum_row.storage_datums_ + rowkey_.datum_array_.count_,
                                           datum_row.count_ - rowkey_.datum_array_.count_))) {
        LOG_WARN("fail to assign datum array", KR(ret));
      } else if (OB_FAIL(deserialize_datum_array.deserialize(buf_, buf_size_, pos))) {
        LOG_WARN("fail to deserialize datum array", KR(ret));
      } else if (OB_UNLIKELY(rowkey_.datum_array_.count_ + deserialize_datum_array.count_ !=
                             datum_row.count_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected column count", KR(ret), K(rowkey_.datum_array_.count_),
                 K(deserialize_datum_array.count_), K(datum_row.count_));
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIMPLE(ObDirectLoadMultipleDatumRow)
{
  OB_TABLE_LOAD_STATISTICS_TIME_COST(DEBUG, external_row_serialize_time_us);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected invalid multiple datum row", KR(ret), KPC(this));
  } else {
    LST_DO_CODE(OB_UNIS_ENCODE,
                rowkey_,
                seq_no_,
                is_delete_,
                is_ack_,
                buf_size_);
    if (OB_SUCC(ret) && buf_size_ > 0) {
      MEMCPY(buf + pos, buf_, buf_size_);
      pos += buf_size_;
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE_SIMPLE(ObDirectLoadMultipleDatumRow)
{
  OB_TABLE_LOAD_STATISTICS_TIME_COST(DEBUG, external_row_deserialize_time_us);
  int ret = OB_SUCCESS;
  reuse();
  LST_DO_CODE(OB_UNIS_DECODE,
              rowkey_,
              seq_no_,
              is_delete_,
              is_ack_,
              buf_size_);
  if (OB_SUCC(ret) && buf_size_ > 0) {
    buf_ = buf + pos;
    pos += buf_size_;
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE_SIMPLE(ObDirectLoadMultipleDatumRow)
{
  OB_TABLE_LOAD_STATISTICS_TIME_COST(DEBUG, external_row_serialize_time_us);
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              rowkey_,
              seq_no_,
              is_delete_,
              is_ack_,
              buf_size_);
  len += buf_size_;
  return len;
}

} // namespace storage
} // namespace oceanbase
