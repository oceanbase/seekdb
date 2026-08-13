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

#ifndef OCEANBASE_MUL_MODE_BIN_BASE
#define OCEANBASE_MUL_MODE_BIN_BASE

#include "lib/string/ob_string_buffer.h"
#include "common/xml/ob_multi_mode_interface.h"

namespace oceanbase {
namespace common {

class ObIMulModeBase;

enum ObMulModeBinType {
  MulModeNull = 0,
  MulModeBoolean,
  MulModeDecimal,
  MulModeUint,
  MulModeInt,
  MulModeTime,
  MulModeDouble,
  MulModeString,
  MulModePair,
  MulModeContainer,
  MulModeMaxType
};

static const ObMulModeBinType g_mul_mode_tc[] = {
  MulModeNull, MulModeDecimal, MulModeInt, MulModeInt, MulModeDouble,
  MulModeString, MulModeContainer, MulModeContainer, MulModeBoolean,
  MulModeTime, MulModeTime, MulModeTime, MulModeTime, MulModeString,
  MulModeContainer, MulModeContainer, MulModeContainer, MulModeContainer,
  MulModeContainer, MulModePair, MulModePair, MulModePair, MulModeString,
  MulModeString, MulModeString, MulModePair, MulModePair, MulModeContainer
};

using ObMulModeExtendStorageType = std::pair<uint8_t, uint8_t>;

enum ObMulModeBinLenSize : uint8_t {
  MBL_UINT8 = 0,
  MBL_UINT16 = 1,
  MBL_UINT32 = 2,
  MBL_UINT64 = 3,
  MBL_MAX = 4,
};

static const uint8_t MUL_MODE_BIN_HEADER_LEN = 2;

struct ObMulModeBinHeader {
  ObMulModeBinHeader() { (&type_)[1] = 0; }
  ObMulModeBinHeader(uint8_t type,
                     uint8_t kv_entry_type,
                     uint8_t count_type,
                     uint8_t obj_type,
                     uint8_t is_continuous)
    : type_(type),
      kv_entry_size_type_(kv_entry_type),
      count_size_type_(count_type),
      obj_size_type_(obj_type),
      is_continuous_(is_continuous),
      reserved_(0)
  {}

  uint8_t type_;
  uint8_t kv_entry_size_type_ : 2;
  uint8_t count_size_type_ : 2;
  uint8_t obj_size_type_ : 2;
  uint8_t is_continuous_ : 1;
  uint8_t reserved_ : 1;
  char used_size_[];
};

class ObMulBinHeaderSerializer {
public:
  ObMulBinHeaderSerializer(ObStringBuffer *buffer,
                           ObMulModeNodeType type,
                           uint64_t total_size,
                           uint64_t count);
  ObMulBinHeaderSerializer(const char *data, uint64_t length);
  ObMulBinHeaderSerializer() = default;

  int serialize();
  int deserialize();
  uint8_t get_obj_var_size() { return obj_var_size_; }
  uint8_t get_entry_var_size() { return entry_var_size_; }
  uint8_t get_count_var_size() { return count_var_size_; }
  void set_obj_size(uint64_t size);
  void set_count(uint64_t size);
  uint64_t get_obj_size() { return total_; }
  uint64_t count() { return count_; }
  ObStringBuffer *buffer() { return buffer_; }
  uint64_t start() { return begin_; }
  uint64_t finish() { return begin_ + obj_var_offset_ + obj_var_size_; }
  uint64_t header_size() { return obj_var_offset_ + obj_var_size_; }
  uint8_t get_obj_var_size_type() { return obj_var_size_type_; }
  uint8_t get_entry_var_size_type() { return entry_var_size_type_; }
  uint8_t get_count_var_size_type() { return count_var_size_type_; }
  ObMulModeNodeType type() { return type_; }

  TO_STRING_KV(K_(obj_var_size_type),
               K_(entry_var_size_type),
               K_(count_var_size_type),
               K_(obj_var_size),
               K_(entry_var_size),
               K_(count_var_size),
               K_(obj_var_offset),
               K_(count_var_offset),
               K_(type),
               K_(total),
               K_(count));

  void set_var_value(uint8_t var_size, uint8_t offset, uint64_t value);

  uint8_t obj_var_size_type_;
  uint8_t entry_var_size_type_;
  uint8_t count_var_size_type_;
  uint8_t obj_var_size_;
  uint8_t entry_var_size_;
  uint8_t count_var_size_;
  uint8_t obj_var_offset_;
  uint8_t count_var_offset_;
  ObMulModeNodeType type_;
  ObStringBuffer *buffer_;
  uint64_t begin_;
  int64_t total_;
  int64_t count_;
  const char *data_;
  uint64_t data_len_;
};

class ObMulModeContainerSerializer {
public:
  ObMulModeContainerSerializer(ObIMulModeBase *root, ObStringBuffer *buffer);
  ObMulModeContainerSerializer(const char *data, int64_t length);
  ObMulModeContainerSerializer(ObIMulModeBase *root, ObStringBuffer *buffer, int64_t children_count);

  bool need_serialize_key() { return root_->data_type() == OB_XML_TYPE || root_->type() == M_OBJECT; }
  bool is_kv_seperate() { return root_->data_type() == OB_XML_TYPE; }

protected:
  ObIMulModeBase *root_;
  ObMulModeNodeType type_;
  int64_t value_entry_start_;
  int64_t value_entry_size_;
  ObMulBinHeaderSerializer header_;
  const char *data_;
  int64_t length_;
};

inline ObMulModeBinType get_mul_mode_tc(ObMulModeNodeType type)
{
  return type >= M_NULL && type < M_MAX_TYPE ? g_mul_mode_tc[type] : MulModeMaxType;
}

inline bool is_extend_type(ObMulModeNodeType type)
{
  return type >= M_EXTENT_LEVEL2 && type <= M_EXTENT_LEVEL0;
}

inline ObMulModeNodeType eval_data_type(ObMulModeNodeType part1, uint8_t)
{
  return static_cast<ObMulModeNodeType>(M_EXTENT_BEGIN0 + 256 * (M_EXTENT_LEVEL0 - part1));
}

inline bool is_scalar_data_type(ObMulModeNodeType type)
{
  const ObMulModeBinType tc = get_mul_mode_tc(type);
  return tc == MulModeNull || tc == MulModeBoolean || tc == MulModeDecimal
      || tc == MulModeInt || tc == MulModeUint || tc == MulModeTime
      || tc == MulModeDouble || tc == MulModeString;
}

inline ObMulModeExtendStorageType get_extend_storage_type(ObMulModeNodeType type)
{
  return ObMulModeExtendStorageType(M_EXTENT_LEVEL0 - ((type - 0x7f) >> 8),
                                    (type & 0xff) - 0x7f);
}

class ObMulModeVar {
public:
  static int read_size_var(const char *data, uint8_t var_size, int64_t *var);
  static int read_var(const char *data, uint8_t type, uint64_t *var);
  static int set_var(uint64_t var, uint8_t type, char *pos);
  static uint64_t get_var_size(uint8_t type);
  static int read_var(const char *data, uint8_t type, int64_t *var);
  static uint8_t get_var_type(int64_t var);
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_MUL_MODE_BIN_BASE
