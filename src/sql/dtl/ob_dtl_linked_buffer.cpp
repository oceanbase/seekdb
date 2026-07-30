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
#include "sql/ob_sql_utils.h"
#include "sql/engine/basic/ob_chunk_row_store.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"

using namespace oceanbase::common;

namespace oceanbase {
namespace sql {
namespace dtl {

OB_SERIALIZE_MEMBER(ObDtlDfoKey, px_sequence_id_, qc_id_, dfo_id_);

OB_SERIALIZE_MEMBER(ObDtlBatchInfo, batch_id_, start_, end_, rows_);

OB_DEF_SERIALIZE(ObDtlOpInfo)
{
  using namespace oceanbase::common;
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, dop_, plan_id_, exec_id_, session_id_, database_id_);
  if (OB_SUCC(ret)) {
    MEMCPY(buf + pos, sql_id_, common::OB_MAX_SQL_ID_LENGTH + 1);
    pos += common::OB_MAX_SQL_ID_LENGTH + 1;
  }
  LST_DO_CODE(OB_UNIS_ENCODE, op_id_, input_rows_, input_width_,
              disable_auto_mem_mgr_, max_batch_size_);
  return ret;
}

OB_DEF_DESERIALIZE(ObDtlOpInfo)
{
  using namespace oceanbase::common;
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, dop_, plan_id_, exec_id_, session_id_, database_id_);
  if (OB_SUCC(ret)) {
    MEMCPY(sql_id_, (char*)buf + pos, common::OB_MAX_SQL_ID_LENGTH + 1);
    pos += common::OB_MAX_SQL_ID_LENGTH + 1;
  }
  LST_DO_CODE(OB_UNIS_DECODE, op_id_, input_rows_, input_width_,
              disable_auto_mem_mgr_, max_batch_size_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDtlOpInfo)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, dop_, plan_id_, exec_id_, session_id_, database_id_);
  len += common::OB_MAX_SQL_ID_LENGTH + 1;
  LST_DO_CODE(OB_UNIS_ADD_LEN, op_id_, input_rows_, input_width_,
              disable_auto_mem_mgr_, max_batch_size_);
  return len;
}

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
    SQL_DTL_LOG(WARN, "decode DTL message header fail", K(size), K(pos), K(ret));
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
          SQL_DTL_LOG(WARN, "push back failed", K(ret));
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

OB_DEF_SERIALIZE(ObDtlLinkedBuffer)
{
  using namespace oceanbase::common;
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(size_);
  if (OB_SUCC(ret)) {
    if (buf_len - pos < size_) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      MEMCPY(buf + pos, buf_, size_);
      pos += size_;
      LST_DO_CODE(OB_UNIS_ENCODE,
        is_data_msg_,
        seq_no_,
        is_eof_,
        timeout_ts_,
        msg_type_,
        flags_,
        dfo_key_,
        use_interm_result_,
        batch_id_,
        batch_info_valid_);
      if (OB_SUCC(ret) && batch_info_valid_) {
        LST_DO_CODE(OB_UNIS_ENCODE, batch_info_);
      }
      if (OB_SUCC(ret)) {
        LST_DO_CODE(OB_UNIS_ENCODE, dfo_id_, sqc_id_);
      }
      if (OB_SUCC(ret)) {
        LST_DO_CODE(OB_UNIS_ENCODE, enable_channel_sync_);
      }
      if (OB_SUCC(ret) && seq_no_ == 1) {
        LST_DO_CODE(OB_UNIS_ENCODE, op_info_);
      }
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObDtlLinkedBuffer)
{
  using namespace oceanbase::common;
  int ret = OB_SUCCESS;
  OB_UNIS_DECODE(size_);
  if (OB_SUCC(ret)) {
    buf_ = (char*)buf + pos;
    pos += size_;
    LST_DO_CODE(OB_UNIS_DECODE,
      is_data_msg_,
      seq_no_,
      
      is_eof_,
      timeout_ts_,
      msg_type_,
      flags_,
      dfo_key_,
      use_interm_result_,
      batch_id_,
      batch_info_valid_);
    if (OB_SUCC(ret) && batch_info_valid_) {
      LST_DO_CODE(OB_UNIS_DECODE, batch_info_);
    }
    if (OB_SUCC(ret)) {
      LST_DO_CODE(OB_UNIS_DECODE, dfo_id_, sqc_id_);
    }
    if (OB_SUCC(ret)) {
      enable_channel_sync_ = false;
      LST_DO_CODE(OB_UNIS_DECODE, enable_channel_sync_);
    }
    if (OB_SUCC(ret) && seq_no_ == 1) {
      LST_DO_CODE(OB_UNIS_DECODE, op_info_);
    }
  }
  if (OB_SUCC(ret)) {
    (void)ObSQLUtils::adjust_time_by_ntp_offset(timeout_ts_);
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDtlLinkedBuffer)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(size_);
  len += size_;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
    is_data_msg_,
    seq_no_,
    is_eof_,
    timeout_ts_,
    msg_type_,
    flags_,
    dfo_key_,
    use_interm_result_,
    batch_id_,
    batch_info_valid_);
  if (batch_info_valid_) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, batch_info_);
  }
  LST_DO_CODE(OB_UNIS_ADD_LEN, dfo_id_, sqc_id_);
  LST_DO_CODE(OB_UNIS_ADD_LEN, enable_channel_sync_);
  if (seq_no_ == 1) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, op_info_);
  }
  return len;
}


}
}
}
