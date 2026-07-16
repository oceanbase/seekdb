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

#define USING_LOG_PREFIX SHARE

#include "share/ob_fts_pos_list_codec.h"

#include "lib/checksum/ob_crc64.h"
#include "lib/utility/serialization.h"

namespace oceanbase
{
namespace share
{

int ObFTSPositionListStore::encode(
    const common::ObIArray<int64_t> &pos_list,
    common::ObIAllocator &allocator,
    common::ObString &encoded_pos_list)
{
  int ret = OB_SUCCESS;
  int64_t payload_len = 0;
  for (int64_t i = 0; i < pos_list.count(); ++i) {
    payload_len += serialization::encoded_length_vi64(pos_list.at(i));
  }
  const CodecType codec_type = VARIABLE_INT64;
  const int64_t header_len =
      serialization::encoded_length_i16(MAGIC_NUMBER) +
      serialization::encoded_length_i16(VERSION) +
      serialization::encoded_length_i16(codec_type) +
      serialization::encoded_length_vi64(payload_len) +
      serialization::encoded_length_i64(static_cast<int64_t>(0)) +
      serialization::encoded_length_vi64(pos_list.count());
  const int64_t total_len = header_len + payload_len;
  char *buf = static_cast<char *>(allocator.alloc(total_len));
  int64_t pos = 0;
  int64_t checksum_pos = 0;
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc pos list buffer", K(ret), K(total_len), K(pos_list.count()));
  } else if (OB_FAIL(serialization::encode_i16(buf, total_len, pos, MAGIC_NUMBER))
          || OB_FAIL(serialization::encode_i16(buf, total_len, pos, VERSION))
          || OB_FAIL(serialization::encode_i16(buf, total_len, pos, codec_type))
          || OB_FAIL(serialization::encode_vi64(buf, total_len, pos, payload_len))) {
    LOG_WARN("failed to encode pos list header", K(ret), K(total_len), K(pos_list.count()));
  } else {
    checksum_pos = pos;
    int64_t checksum_placeholder = 0;
    if (OB_FAIL(serialization::encode_i64(buf, total_len, pos, checksum_placeholder))
        || OB_FAIL(serialization::encode_vi64(buf, total_len, pos, pos_list.count()))) {
      LOG_WARN("failed to encode pos list header", K(ret), K(total_len), K(pos_list.count()));
    } else if (OB_UNLIKELY(pos != header_len)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected pos list header length", K(ret), K(pos), K(header_len));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < pos_list.count(); ++i) {
        if (OB_FAIL(serialization::encode_vi64(buf, total_len, pos, pos_list.at(i)))) {
          LOG_WARN("failed to encode pos list element", K(ret), K(i), K(pos_list.at(i)), K(total_len), K(pos));
        }
      }
    }
    if (OB_SUCC(ret)) {
      const int64_t actual_payload_len = pos - header_len;
      if (OB_UNLIKELY(actual_payload_len != payload_len)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected pos list payload length", K(ret), K(actual_payload_len), K(payload_len));
      } else {
        const int64_t checksum = payload_len == 0
            ? 0
            : static_cast<int64_t>(common::ob_crc64(buf + header_len, payload_len));
        int64_t tmp_pos = checksum_pos;
        if (OB_FAIL(serialization::encode_i64(buf, total_len, tmp_pos, checksum))) {
          LOG_WARN("failed to patch pos list checksum", K(ret), K(checksum), K(checksum_pos), K(total_len));
        }
      }
    }
    if (OB_SUCC(ret)) {
      encoded_pos_list.assign_ptr(buf, static_cast<int32_t>(pos));
    }
  }
  return ret;
}

int ObFTSPositionListStore::decode(
    const common::ObString &encoded_pos_list,
    common::ObArray<int64_t, common::ObIAllocator &> &pos_list)
{
  int ret = OB_SUCCESS;
  pos_list.reuse();
  int64_t pos = 0;
  int16_t magic = 0;
  int16_t version = 0;
  int16_t codec_type = VARIABLE_INT64;
  int64_t payload_len = 0;
  int64_t checksum = 0;
  int64_t pos_cnt = 0;
  if (encoded_pos_list.empty()) {
  } else if (OB_FAIL(serialization::decode_i16(encoded_pos_list.ptr(), encoded_pos_list.length(), pos, &magic))
          || OB_FAIL(serialization::decode_i16(encoded_pos_list.ptr(), encoded_pos_list.length(), pos, &version))
          || OB_FAIL(serialization::decode_i16(encoded_pos_list.ptr(), encoded_pos_list.length(), pos, &codec_type))
          || OB_FAIL(serialization::decode_vi64(encoded_pos_list.ptr(), encoded_pos_list.length(), pos, &payload_len))
          || OB_FAIL(serialization::decode_i64(encoded_pos_list.ptr(), encoded_pos_list.length(), pos, &checksum))
          || OB_FAIL(serialization::decode_vi64(encoded_pos_list.ptr(), encoded_pos_list.length(), pos, &pos_cnt))) {
    LOG_WARN("failed to decode pos list header", K(ret), K(encoded_pos_list.length()));
  } else if (OB_UNLIKELY(magic != MAGIC_NUMBER || version != VERSION
                         || (codec_type != VARIABLE_INT64 && codec_type != DELTA_ZIGZAG_PFOR)
                         || payload_len < 0
                         || pos + payload_len > encoded_pos_list.length())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid pos list header", K(ret), K(magic), K(version), K(codec_type), K(payload_len), K(pos), K(encoded_pos_list.length()));
  } else {
    const common::ObString payload(static_cast<int32_t>(payload_len), encoded_pos_list.ptr() + pos);
    const int64_t calc_checksum = payload.empty() ? 0 : static_cast<int64_t>(common::ob_crc64(payload.ptr(), payload.length()));
    if (OB_UNLIKELY(calc_checksum != checksum)) {
      ret = OB_CHECKSUM_ERROR;
      LOG_WARN("pos list checksum mismatch", K(ret), K(calc_checksum), K(checksum), K(payload_len));
    } else if (VARIABLE_INT64 == codec_type) {
      if (OB_FAIL(decode_with_variable_int64(payload, pos_list))) {
        LOG_WARN("failed to decode variable-int64 pos list payload", K(ret), K(payload_len), K(pos_cnt));
      }
    } else if (OB_FAIL(decode_with_delta_zigzag_pfor(payload, pos_list))) {
      LOG_WARN("failed to decode delta-zigzag-pfor pos list payload", K(ret), K(payload_len), K(pos_cnt));
    } else if (OB_UNLIKELY(pos_list.count() != pos_cnt)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("pos list count mismatch", K(ret), K(pos_list.count()), K(pos_cnt));
    }
  }
  return ret;
}

int ObFTSPositionListStore::encode_with_variable_int64(
    const common::ObIArray<int64_t> &pos_list,
    common::ObIAllocator &allocator,
    common::ObString &payload)
{
  int ret = OB_SUCCESS;
  int64_t payload_len = 0;
  for (int64_t i = 0; i < pos_list.count(); ++i) {
    payload_len += serialization::encoded_length_vi64(pos_list.at(i));
  }
  char *buf = static_cast<char *>(allocator.alloc(payload_len));
  int64_t pos = 0;
  if (OB_ISNULL(buf) && payload_len > 0) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc variable-int pos list payload", K(ret), K(payload_len), K(pos_list.count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < pos_list.count(); ++i) {
      if (OB_FAIL(serialization::encode_vi64(buf, payload_len, pos, pos_list.at(i)))) {
        LOG_WARN("failed to encode pos list element", K(ret), K(i), K(pos_list.at(i)), K(payload_len), K(pos));
      }
    }
    if (OB_SUCC(ret)) {
      payload.assign_ptr(buf, static_cast<int32_t>(pos));
    }
  }
  return ret;
}

int ObFTSPositionListStore::decode_with_variable_int64(
    const common::ObString &payload,
    common::ObArray<int64_t, common::ObIAllocator &> &pos_list)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  pos_list.reuse();
  while (OB_SUCC(ret) && pos < payload.length()) {
    int64_t value = 0;
    if (OB_FAIL(serialization::decode_vi64(payload.ptr(), payload.length(), pos, &value))) {
      LOG_WARN("failed to decode pos list value", K(ret), K(pos), K(payload.length()));
    } else if (OB_FAIL(pos_list.push_back(value))) {
      LOG_WARN("failed to push pos list value", K(ret), K(value), K(pos_list.count()));
    }
  }
  return ret;
}

int ObFTSPositionListStore::encode_with_delta_zigzag_pfor(
    const common::ObIArray<int64_t> &pos_list,
    common::ObIAllocator &allocator,
    common::ObString &payload)
{
  UNUSEDx(pos_list, allocator, payload);
  return OB_NOT_SUPPORTED;
}

int ObFTSPositionListStore::decode_with_delta_zigzag_pfor(
    const common::ObString &payload,
    common::ObArray<int64_t, common::ObIAllocator &> &pos_list)
{
  UNUSEDx(payload, pos_list);
  return OB_NOT_SUPPORTED;
}

} // namespace share
} // namespace oceanbase
