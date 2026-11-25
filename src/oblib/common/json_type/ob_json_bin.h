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

#pragma once

#include "ob_json_common.h"
#include "ob_json_base.h"
#include "common/number/ob_number_v2.h"

namespace oceanbase {
namespace common {

class ObJsonVerType
{
public:
  static bool is_opaque_or_string(ObJsonNodeType type);
};

class ObJsonBinUpdateCtx;
struct ObJsonBinCtx
{
  ObJsonBinCtx() : extend_seg_offset_(0), update_ctx_(nullptr), is_update_ctx_alloc_(false) {}
  ~ObJsonBinCtx();
  int64_t extend_seg_offset_;
  ObJsonBinUpdateCtx *update_ctx_;
  bool is_update_ctx_alloc_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObJsonBinCtx);
};

class ObJsonBin : public ObIJsonBase
{
public:
  ObJsonBin() : ObJsonBin(static_cast<ObIAllocator *>(nullptr)) {}
  explicit ObJsonBin(ObIAllocator *allocator)
      : ObIJsonBase(allocator), allocator_(allocator), cursor_(&local_cursor_),
        pos_(0), root_pos_(0), int_val_(0), prec_(-1), scale_(-1), ctx_(nullptr), is_alloc_ctx_(false)
  {}
  explicit ObJsonBin(ObIAllocator *allocator, ObJsonBinCtx *ctx, bool is_alloc_ctx = false)
      : ObJsonBin(allocator)
  {
    ctx_ = ctx;
    is_alloc_ctx_ = is_alloc_ctx;
  }
  explicit ObJsonBin(const char *data, int64_t length, ObIAllocator *allocator = nullptr)
      : ObIJsonBase(allocator), allocator_(allocator), cursor_(&local_cursor_),
        local_cursor_(data, length), pos_(0), root_pos_(0), int_val_(0), prec_(-1), scale_(-1),
        ctx_(nullptr), is_alloc_ctx_(false)
  {}
  explicit ObJsonBin(const char *data, int64_t length, ObJsonBinCtx *ctx)
      : ObJsonBin(data, length, static_cast<ObIAllocator *>(nullptr))
  {
    ctx_ = ctx;
  }
  virtual ~ObJsonBin() { destroy(); }

  bool get_boolean() const override { return static_cast<bool>(uint_val_); }
  double get_double() const override { return double_val_; }
  float get_float() const override { return float_val_; }
  int64_t get_int() const override { return int_val_; }
  uint64_t get_uint() const override { return uint_val_; }
  uint64_t get_data_length() const override
  {
    return json_type() == ObJsonNodeType::J_ARRAY || json_type() == ObJsonNodeType::J_OBJECT
        ? meta_.bytes_ : meta_.element_count_;
  }
  number::ObNumber get_decimal_data() const override
  {
    number::ObNumber number;
    number.shadow_copy(number_);
    return number;
  }
  ObPrecision get_decimal_precision() const override { return prec_; }
  ObScale get_decimal_scale() const override { return scale_; }
  int get_obtime(ObTime &time) const override;
  ObJsonInType get_internal_type() const override { return ObJsonInType::JSON_BIN; }
  uint64_t element_count() const override { return meta_.element_count_; }
  ObJsonNodeType json_type() const override { return meta_.value_type_; }
  ObObjType field_type() const override { return meta_.field_type_; }
  uint64_t member_count() const override
  {
    return json_type() == ObJsonNodeType::J_ARRAY || json_type() == ObJsonNodeType::J_OBJECT
        ? meta_.element_count_ : 1;
  }
  int get_array_element(uint64_t index, ObIJsonBase *&value) const override;
  int get_object_value(uint64_t index, ObIJsonBase *&value) const override;
  int get_object_value(const ObString &key, ObIJsonBase *&value) const override;
  int get_object_value(uint64_t index, ObString &key, ObIJsonBase *&value) const override;
  int get_key(uint64_t index, ObString &key) const override;
  int get_raw_binary(ObString &output, ObIAllocator *allocator) const;
  int get_used_bytes(uint64_t &size) const;
  int get_free_space(size_t &space) const;
  const char *get_data() const override;
  uint32_t depth() const override;
  int reset() override;
  int reset_iter();
  int reset(const ObString &buffer, int64_t offset, ObJsonBinCtx *ctx);
  int element(size_t index);
  int lookup(const ObString &key);
  int should_pack_diff(bool &should_pack) const;
  const ObILobCursor *get_cursor() const { return cursor_; }
  ObILobCursor *get_cursor() { return cursor_; }
  ObJsonBinUpdateCtx *get_update_ctx() const { return ctx_ == nullptr ? nullptr : ctx_->update_ctx_; }

private:
  struct Metadata
  {
    uint8_t raw_type_ = 0;
    ObJsonNodeType value_type_ = ObJsonNodeType::J_NULL;
    uint8_t entry_size_ = 0;
    uint64_t element_count_ = 0;
    uint64_t bytes_ = 0;
    ObObjType field_type_ = ObNullType;
    uint64_t str_data_offset_ = 0;
  };
  int native_child(size_t index, uint64_t &offset, uint8_t &type, uint8_t &entry_type) const;
  int init_bin_data();
  int init_cursor(const ObString &data);
  int reset(uint8_t type, int64_t offset, uint8_t entry_type);
  int reset(int64_t offset);
  int reset(uint8_t type, const ObString &buffer, int64_t offset, uint8_t entry_type, ObJsonBinCtx *ctx);
  int reset_child(ObJsonBin &child, uint8_t type, int64_t offset, uint8_t entry_type) const;
  int get_element_v0(size_t index);
  int get_key_in_object(size_t index, ObString &key) const;
  int check_valid_object_op(uint64_t index) const;
  int check_valid_array_op(uint64_t index) const;
  int create_new_binary(ObIJsonBase *value, ObJsonBin *&output) const;
  int lookup_index(const ObString &key, size_t *index) const;
  bool is_empty_data() const;
  bool is_at_root() const { return pos_ == root_pos_; }
  void destroy();

  ObIAllocator *allocator_;
  Metadata meta_;
  ObILobCursor *cursor_;
  ObLobInRowCursor local_cursor_;
  int64_t pos_;
  int64_t root_pos_;
  union {
    int64_t int_val_;
    uint64_t uint_val_;
    double double_val_;
    float float_val_;
  };
  number::ObNumber number_;
  ObPrecision prec_;
  ObScale scale_;
  ObJsonBinCtx *ctx_;
  bool is_alloc_ctx_;
  DISALLOW_COPY_AND_ASSIGN(ObJsonBin);
};

} // namespace common
} // namespace oceanbase
