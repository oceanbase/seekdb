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
#define USING_LOG_PREFIX SQL_DTL
#include "ob_dtl_msg.h"
#include "ob_dtl_linked_buffer.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"

using namespace oceanbase::common;

namespace oceanbase {
namespace sql {
namespace dtl {

int ObDtlLinkedBuffer::deserialize_msg_header(const ObDtlLinkedBuffer &buffer,
                                              ObDtlMsgHeader &header,
                                              bool keep_pos /*= false*/)
{
  int ret = OB_SUCCESS;
  const char *buf = buffer.buf();
  int64_t size = buffer.size();
  int64_t &pos = buffer.pos();
  int64_t old_pos = buffer.pos();
  if (pos == size) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(serialization::decode(buf, size, pos, header))) {
  } else if (header.type_ >= static_cast<int16_t>(ObDtlMsgType::MAX)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_DTL_LOG(WARN, "channel has received message with unknown type",
                K(header), K(size), K(pos));
  }
  if (keep_pos) {
    buffer.pos() = old_pos;
  }
  return ret;
}

int ObDtlLinkedBuffer::add_batch_info(int64_t batch_id, int64_t rows)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(!is_data_msg())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_DTL_LOG(WARN, "unexpected data msg", K(ret), K(is_data_msg()));
  } else {
    int64_t header_size = 0;
    switch (msg_type_) {
      case PX_DATUM_ROW: {
        header_size = sizeof(ObChunkDatumStore::Block);
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        SQL_DTL_LOG(WARN, "unexpected msg type", K(ret), K(msg_type_));
      }
    }
    if (OB_SUCC(ret)) {
      const int64_t count = batch_info_.count();
      const int64_t start = 0 == count ? header_size : batch_info_.at(count - 1).end_;
      if (OB_UNLIKELY(pos_ < start || rows < rows_cnt_)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_DTL_LOG(WARN, "unexpected start and pos", K(ret), K(pos_), K(start), K(rows_cnt_),
                    K(rows), K(batch_info_));
      } else {
        ObDtlBatchInfo info(batch_id, start, pos_, rows - rows_cnt_);
        if (OB_FAIL(batch_info_.push_back(info))) {
        } else {
          batch_info_valid_ = true;
          rows_cnt_ = rows;
        }
      }
    }
  }
  return ret;
}

int ObDtlLinkedBuffer::push_batch_id(int64_t batch_id, int64_t rows)
{
  int ret = common::OB_SUCCESS;
  if (batch_info_valid_) {
    ret = add_batch_info(batch_id, rows);
  } else {
    batch_id_ = batch_id;
  }
  return ret;
}

}
}
}
