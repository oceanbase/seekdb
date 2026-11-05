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
#include "ob_macro_block_common_header.h"
#include "ob_block_sstable_struct.h"

namespace oceanbase
{
using namespace common;
namespace blocksstable
{
ObMacroBlockCommonHeader::ObMacroBlockCommonHeader()
{
  reset();
}

void ObMacroBlockCommonHeader::reset()
{
  header_size_ = (int32_t)get_serialize_size();
  version_ = MACRO_BLOCK_COMMON_HEADER_VERSION;
  magic_ = MACRO_BLOCK_COMMON_HEADER_MAGIC;
  attr_ = MacroBlockType::None;
  payload_size_ = 0;
  payload_checksum_ = 0;
}

int ObMacroBlockCommonHeader::set_attr(const MacroBlockType type)
{
  int ret = OB_SUCCESS;
  if (type >= MacroBlockType::MaxMacroType || type <= MacroBlockType::None) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid data store type", K(ret), K(type));
  } else {
    attr_ = type;
  }
  return ret;
}

int ObMacroBlockCommonHeader::build_serialized_header(char *buf, const int64_t len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (OB_FAIL(serialize(buf, len, pos))) {
    LOG_ERROR("fail to serialize record header, ", K(ret), KP(buf), K(len), K(pos), K(*this));
  } else if (get_serialize_size() != pos) {
    ret = OB_SERIALIZE_ERROR;
    LOG_ERROR("serialize size mismatch, ", K(ret), K(pos), K(*this));
  }
  return ret;
}

int ObMacroBlockCommonHeader::check_integrity() const
{
  int ret =OB_SUCCESS;
  if (header_size_ != get_serialize_size()
      || version_ != MACRO_BLOCK_COMMON_HEADER_VERSION
      || magic_ != MACRO_BLOCK_COMMON_HEADER_MAGIC) {
    ret = OB_INVALID_DATA;
    LOG_WARN("invalid common header", K(ret), K(*this));
  }
  return ret;
}

bool ObMacroBlockCommonHeader::is_valid() const
{
  bool b_ret = header_size_ > 0
      && version_ == MACRO_BLOCK_COMMON_HEADER_VERSION
      && MACRO_BLOCK_COMMON_HEADER_MAGIC == magic_
      && attr_ >= MacroBlockType::None
      && attr_ < MacroBlockType::MaxMacroType;
  return b_ret;
}

int ObMacroBlockCommonHeader::serialize(char *buf,
                                        const int64_t buf_len,
                                        int64_t& pos) const
{
  int ret = OB_SUCCESS;
  if (NULL == buf || buf_len < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument.", K(ret), KP(buf), K(buf_len));
  } else if (pos + get_serialize_size() > buf_len) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_ERROR("data buffer is not enough", K(ret), K(pos), K(buf_len), K(*this));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("common header is invalid", K(ret), K(*this));
  } else {
    ObMacroBlockCommonHeader *common_header = reinterpret_cast<ObMacroBlockCommonHeader*>(buf + pos);
    common_header->header_size_ = header_size_;
    common_header->version_ = version_;
    common_header->magic_ = magic_;
    common_header->attr_ = attr_;
    common_header->payload_size_ = payload_size_;
    common_header->payload_checksum_ = payload_checksum_;
    pos += common_header->get_serialize_size();
  }
  return ret;
}

int ObMacroBlockCommonHeader::deserialize(const char *buf,
                                          const int64_t data_len,
                                          int64_t& pos)
{
  int ret = OB_SUCCESS;
  if (NULL == buf || data_len <= 0 || pos < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument.", K(ret), KP(buf), K(data_len), K(pos));
  } else {
    const ObMacroBlockCommonHeader *ptr = reinterpret_cast<const ObMacroBlockCommonHeader*>(buf + pos);
    header_size_ = ptr->header_size_;
    version_ = ptr->version_;
    magic_ = ptr->magic_;
    attr_ = ptr->attr_;
    payload_size_ = ptr->payload_size_;
    payload_checksum_ = ptr->payload_checksum_;

    if (OB_UNLIKELY(!is_valid())) {
      ret = OB_DESERIALIZE_ERROR;
      LOG_ERROR("deserialize error", K(ret), K(*this));
    } else {
      pos += get_serialize_size();
    }
  }
  return ret;
}

} // blocksstable
} // oceanbase
