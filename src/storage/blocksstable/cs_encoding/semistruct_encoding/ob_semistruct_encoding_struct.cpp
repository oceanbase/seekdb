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

#include "ob_semistruct_encoding_struct.h"
#include "storage/blocksstable/cs_encoding/ob_column_encoding_struct.h"
#include "storage/blocksstable/cs_encoding/ob_cs_encoding_util.h"

namespace oceanbase
{
namespace blocksstable
{
using namespace common;

int ObSemiStructEncodeMetaDesc::deserialize(
    const ObCSColumnHeader &col_header,
    const uint32_t row_cnt,
    const char *buf,
    const int64_t len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || pos + sizeof(ObSemiStructEncodeHeader) > len) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("buf is invalid", K(ret), KP(buf), K(len), K(pos), "size", sizeof(ObSemiStructEncodeHeader));
  } else {
    int64_t offset = pos;
    semistruct_header_ = reinterpret_cast<const ObSemiStructEncodeHeader *>(buf + offset);
    offset += sizeof(ObSemiStructEncodeHeader);
    if (semistruct_header_->type_ != ObSemiStructEncodeHeader::Type::JSON) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("semistruct_header is incorrect", K(ret), KPC(semistruct_header_));
    } else if (pos + semistruct_header_->header_len_ > len) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("buf is invalid", K(ret), KP(buf), K(len), K(pos), KPC(semistruct_header_));
    } else {
      uint16_t sub_column_cnt = semistruct_header_->column_cnt_;
      sub_col_headers_ = reinterpret_cast<const ObCSColumnHeader*>(buf + offset);
      offset += sizeof(ObCSColumnHeader) * sub_column_cnt;

      sub_schema_data_ptr_ = buf + offset;
      offset += semistruct_header_->schema_len_;

      if (col_header.has_null_bitmap()) {
        bitmap_size_ = ObCSEncodingUtil::get_bitmap_byte_size(row_cnt);
        null_bitmap_ = buf + offset;
        offset += bitmap_size_;
      } else {
        bitmap_size_ = 0;
        null_bitmap_ = nullptr;
      }

      sub_col_meta_ptr_ = buf + offset;
      sub_col_meta_len_ = semistruct_header_->header_len_ - offset;

      pos += semistruct_header_->header_len_;
    }
  }
  return ret;
}

}  // end namespace blocksstable
}  // end namespace oceanbase
