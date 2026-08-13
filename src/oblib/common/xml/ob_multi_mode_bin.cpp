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
#define USING_LOG_PREFIX LIB

#include "common/xml/ob_multi_mode_bin.h"

namespace oceanbase {
namespace common {

class ObIMulModeBase;



ObMulBinHeaderSerializer::ObMulBinHeaderSerializer(
  ObStringBuffer* buffer, 
  ObMulModeNodeType type, 
  uint64_t total_size, 
  uint64_t count) 
  : buffer_(buffer),
    begin_(buffer->length()),
    total_(total_size),
    count_(count)
{
  type_ = type;

  obj_var_size_type_ = ObMulModeVar::get_var_type(total_);
  entry_var_size_type_ = ObMulModeVar::get_var_type(total_);
  count_var_size_type_ = ObMulModeVar::get_var_type(count_);

  obj_var_size_ = ObMulModeVar::get_var_size(obj_var_size_type_);
  entry_var_size_ = obj_var_size_;
  count_var_size_ = ObMulModeVar::get_var_size(count_var_size_type_);
  
  count_var_offset_ = MUL_MODE_BIN_HEADER_LEN;
  if (is_extend_type(type)) {
    count_var_offset_++;
  }
  
  obj_var_offset_ = count_var_offset_ + count_var_size_;
}

void ObMulBinHeaderSerializer::set_var_value(uint8_t var_size, uint8_t offset, uint64_t value)
{
  if (var_size == 1) {
    *reinterpret_cast<uint8_t*>(buffer_->ptr() + begin_ + offset) = static_cast<uint8_t>(value);
  } else if (var_size == 2) {
    *reinterpret_cast<uint16_t*>(buffer_->ptr() + begin_ + offset) = static_cast<uint16_t>(value);
  } else if (var_size == 4) {
    *reinterpret_cast<uint32_t*>(buffer_->ptr() + begin_ + offset) = static_cast<uint32_t>(value);
  } else {
    *reinterpret_cast<uint64_t*>(buffer_->ptr() + begin_ + offset) = value;
  }
}

void ObMulBinHeaderSerializer::set_obj_size(uint64_t size)
{
  set_var_value(obj_var_size_, obj_var_offset_, size);
}

void ObMulBinHeaderSerializer::set_count(uint64_t size)
{
  set_var_value(count_var_size_, count_var_offset_, size);
}

ObMulBinHeaderSerializer::ObMulBinHeaderSerializer(const char* data, uint64_t length)
  : data_(data),
    data_len_(length)
{
}

int ObMulBinHeaderSerializer::serialize()
{
  INIT_SUCC(ret);
  if (OB_FAIL(buffer_->reserve(MUL_MODE_BIN_HEADER_LEN))) {
  } else if (is_scalar_data_type(type_)) {
    if (is_extend_type(type_)) {
      ObMulModeExtendStorageType tmp = get_extend_storage_type(type_);
      if (OB_FAIL(buffer_->append(reinterpret_cast<const char*>(&tmp.first), sizeof(uint8_t)))
          || OB_FAIL(buffer_->append(reinterpret_cast<const char*>(&tmp.second), sizeof(uint8_t))))
      LOG_WARN("failed to append", K(ret), K(buffer_->length()));
    } else if (OB_FAIL(buffer_->append(reinterpret_cast<const char*>(&type_), sizeof(uint8_t)))) {
    }
  } else if (OB_FAIL(buffer_->reserve(header_size()))) {
  } else {
    buffer_->set_length(start() + header_size());
    new (buffer_->ptr() + start())ObMulModeBinHeader(static_cast<uint8_t>(type_),
                                                      ObMulModeVar::get_var_type(total_),
                                                      ObMulModeVar::get_var_type(count_),
                                                      ObMulModeVar::get_var_type(total_),
                                                      static_cast<uint8_t>(1));
    
    if (is_extend_type(type_)) {
      ObMulModeExtendStorageType tmp = get_extend_storage_type(type_);
      *reinterpret_cast<uint8_t*>(buffer_->ptr() + start()) = static_cast<uint8_t>(tmp.first);
      *reinterpret_cast<uint8_t*>(buffer_->ptr() + start() + MUL_MODE_BIN_HEADER_LEN) = static_cast<uint8_t>(tmp.first);
    }
    set_obj_size(total_);
    set_count(count_);
  }

  return ret;
}

int ObMulBinHeaderSerializer::deserialize()
{
  INIT_SUCC(ret);
  if (data_len_ < 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("failed to deserialize, data len is 0", K(ret));
  } else {
    type_ = static_cast<ObMulModeNodeType>(*data_);
    if (is_scalar_data_type(type_) && is_extend_type(type_)) {
      if (data_len_ <= 2) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("failed to deserialize, data len is 2", K(ret), K(type_), K(data_len_));
      } else {
        type_ = eval_data_type(type_, static_cast<uint8_t>(data_[1]));
      }
    } else if (is_scalar_data_type(type_)) {
    } else if (data_len_ <= 2) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("failed to deserialize, data len less than 2", K(ret), K(type_), K(data_len_));
    } else {
      const ObMulModeBinHeader* header = reinterpret_cast<const ObMulModeBinHeader*>(data_);
      obj_var_size_ = ObMulModeVar::get_var_size(header->obj_size_type_);
      entry_var_size_ = ObMulModeVar::get_var_size(header->kv_entry_size_type_);
      count_var_size_ = ObMulModeVar::get_var_size(header->count_size_type_);
      count_var_offset_ = MUL_MODE_BIN_HEADER_LEN;

      obj_var_size_type_ = header->obj_size_type_;
      entry_var_size_type_ = header->kv_entry_size_type_;
      count_var_size_type_ = header->count_size_type_;

      if (is_extend_type(type_)) {
        count_var_offset_++;
      }
      obj_var_offset_ = count_var_offset_ + count_var_size_;

      if (obj_var_offset_ + obj_var_size_ > data_len_) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("failed to deserialize, data len less than 2", K(ret), K(type_), 
                K(data_len_), K(entry_var_size_), K(count_var_size_), K(obj_var_size_));
      } else {
        if (is_extend_type(type_)) {
          type_ = eval_data_type(type_, data_[2]);
        }
        ObMulModeVar::read_size_var(data_ + obj_var_offset_, obj_var_size_, &total_);
        ObMulModeVar::read_size_var(data_ + count_var_offset_, count_var_size_, &count_);
      }
    }
  }
  
  return ret;
}









ObMulModeContainerSerializer::ObMulModeContainerSerializer(ObIMulModeBase* root, ObStringBuffer* buffer, int64_t children_count)
  : header_(buffer, root->type(), root->get_serialize_size(), children_count)
{
  root_ = root;
  type_ = root->type();
}

ObMulModeContainerSerializer::ObMulModeContainerSerializer(ObIMulModeBase* root, ObStringBuffer* buffer)
  : header_(buffer, root->type(), root->get_serialize_size(), root->size())
{
  root_ = root;
  type_ = root->type();
}

ObMulModeContainerSerializer::ObMulModeContainerSerializer(const char* data, int64_t length)
  : header_(data, length),
    data_(data),
    length_(length)
{
}

/* var size */
int ObMulModeVar::read_var(const char *data, uint8_t type, uint64_t *var)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(data)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("input data null val.", K(ret));
  } else {
    ObMulModeBinLenSize size = static_cast<ObMulModeBinLenSize>(type);
    switch (size) {
      case MBL_UINT8: {
        *var = static_cast<uint64_t>(*reinterpret_cast<const uint8_t*>(data));
        break;
      }
      case MBL_UINT16: {
        *var = static_cast<uint64_t>(*reinterpret_cast<const uint16_t*>(data));
        break;
      }
      case MBL_UINT32: {
        *var = static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(data));
        break;
      }
      case MBL_UINT64: {
        *var = static_cast<uint64_t>(*reinterpret_cast<const uint64_t*>(data));
        break;
      }
      default: {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("invalid var type.", K(ret), K(type));
        break;
      }
    }
  }
  return ret;
}

int ObMulModeVar::read_size_var(const char *data, uint8_t var_size, int64_t *var)
{
  INIT_SUCC(ret);
  if (var_size == 1) {
    *var = *reinterpret_cast<const int8_t*> (data);
  } else if (var_size == 2) {
    *var = *reinterpret_cast<const int16_t*>(data);
  } else if (var_size == 4) {
    *var = *reinterpret_cast<const int32_t*>(data);
  } else if (var_size == 8) {
    *var = *reinterpret_cast<const int64_t*>(data);
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("invalid var type.", K(ret), K(var_size));
  }
  return ret;
}



int ObMulModeVar::set_var(uint64_t var, uint8_t type, char *pos)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(pos)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("output pos is null.", K(ret));
  } else {
    ObMulModeBinLenSize size = static_cast<ObMulModeBinLenSize>(type);
    switch (size) {
      case MBL_UINT8: {
        uint8_t *val_pos = reinterpret_cast<uint8_t*>(pos);
        *val_pos = static_cast<uint8_t>(var);
        break;
      }
      case MBL_UINT16: {
        uint16_t *val_pos = reinterpret_cast<uint16_t*>(pos);
        *val_pos = static_cast<uint16_t>(var);
        break;
      }
      case MBL_UINT32: {
        uint32_t *val_pos = reinterpret_cast<uint32_t*>(pos);
        *val_pos = static_cast<uint32_t>(var);
        break;
      }
      case MBL_UINT64: {
        uint64_t *val_pos = reinterpret_cast<uint64_t*>(pos);
        *val_pos = static_cast<uint64_t>(var);
        break;
      }
      default: {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("invalid var type.", K(ret), K(size));
        break;
      }
    }
  }
  return ret;
}

uint64_t ObMulModeVar::get_var_size(uint8_t type)
{
  uint64_t var_size = MBL_MAX;
  ObMulModeBinLenSize size = static_cast<ObMulModeBinLenSize>(type);
  switch (size) {
    case MBL_UINT8: {
      var_size = sizeof(uint8_t);
      break;
    }
    case MBL_UINT16: {
      var_size = sizeof(uint16_t);
      break;
    }
    case MBL_UINT32: {
      var_size = sizeof(uint32_t);
      break;
    }
    case MBL_UINT64: {
      var_size = sizeof(uint64_t);
      break;
    }
    default: {
      LOG_WARN_RET(OB_ERR_UNEXPECTED, "invalid var type.", K(OB_NOT_SUPPORTED), K(size));
      break;
    }
  }
  return var_size;
}


int ObMulModeVar::read_var(const char *data, uint8_t type, int64_t *var)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(data)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("input data is null.", K(ret));
  } else {
    ObMulModeBinLenSize size = static_cast<ObMulModeBinLenSize>(type);
    switch (size) {
      case MBL_UINT8: {
        *var = static_cast<int64_t>(*reinterpret_cast<const int8_t*>(data));
        break;
      }
      case MBL_UINT16: {
        *var = static_cast<int64_t>(*reinterpret_cast<const int16_t*>(data));
        break;
      }
      case MBL_UINT32: {
        *var = static_cast<int64_t>(*reinterpret_cast<const int32_t*>(data));
        break;
      }
      case MBL_UINT64: {
        *var = static_cast<int64_t>(*reinterpret_cast<const int64_t*>(data));
        break;
      }
      default: {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("invalid var type.", K(ret), K(type));
        break;
      }
    }
  }
  return ret;
}



uint8_t ObMulModeVar::get_var_type(int64_t var)
{
  ObMulModeBinLenSize lsize = MBL_UINT64;
  if (var <= INT8_MAX && var >= INT8_MIN) {
    lsize = MBL_UINT8;
  } else if (var <= INT16_MAX && var >= INT16_MIN) {
    lsize = MBL_UINT16;
  } else if (var <= INT32_MAX && var >= INT32_MIN) {
    lsize = MBL_UINT32;
  }
  return static_cast<uint8_t>(lsize);
}

} // namespace common
} // namespace oceanbase
