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
#include "ob_json_bin.h"
#include "ob_json_diff.h"
#include "rust/json.h"

namespace oceanbase {
namespace common {


namespace {
int stored_value_status(int status, int source_status)
{
  return status == 0 ? OB_SUCCESS : status == 1 ? OB_ALLOCATE_MEMORY_FAILED
      : status == 2 ? source_status : OB_ERR_UNEXPECTED;
}
}

int ObJsonBin::get_obtime(ObTime &t) const
{
  INIT_SUCC(ret);

  switch (json_type()) {
    case ObJsonNodeType::J_DATE:
    case ObJsonNodeType::J_JSON_DATE_EXT: {
      ret = ObTimeConverter::date_to_ob_time(int_val_, t);
      break;
    }
    case ObJsonNodeType::J_MYSQL_DATE: {
      t.mode_ |= DT_TYPE_MYSQL_DATE;
      ret = ObTimeConverter::mdate_to_ob_time(int_val_, t);
      break;
    }
    case ObJsonNodeType::J_TIME: {
      ret = ObTimeConverter::time_to_ob_time(int_val_, t);
      break;
    }
    case ObJsonNodeType::J_DATETIME:
    case ObJsonNodeType::J_TIMESTAMP: 
    case ObJsonNodeType::J_ODATE:
    case ObJsonNodeType::J_OTIMESTAMP:
    case ObJsonNodeType::J_OTIMESTAMPTZ: {
      ret = ObTimeConverter::datetime_to_ob_time(int_val_, NULL, t);
      break;
    }
    case ObJsonNodeType::J_MYSQL_DATETIME: {
      t.mode_ |= DT_TYPE_MYSQL_DATETIME;
      ret = ObTimeConverter::mdatetime_to_ob_time(int_val_, t);
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }
  if (OB_FAIL(ret)) {
  }

  return ret;
}

int ObJsonBin::check_valid_object_op(uint64_t index) const
{
  INIT_SUCC(ret);

  if (index >= element_count()) { // check param
    ret = OB_OUT_OF_ELEMENT;
  } else if (json_type() != ObJsonNodeType::J_OBJECT) { // check json node type
    ret = OB_INVALID_ARGUMENT;
  }

  return ret;
}

int ObJsonBin::check_valid_array_op(uint64_t index) const
{
  INIT_SUCC(ret);

  if (index >= element_count()) { // check param
    ret = OB_OUT_OF_ELEMENT;
  } else if (json_type() != ObJsonNodeType::J_ARRAY) { // check json node type
    ret = OB_INVALID_ARGUMENT;
  } 

  return ret;
}

int ObJsonBin::get_key(uint64_t index, common::ObString &key_out) const
{
  INIT_SUCC(ret);

  if (OB_FAIL(check_valid_object_op(index))) {
  } else {
    if (OB_FAIL(get_key_in_object(index, key_out))) {
    }
  }

  return ret;
}

int ObJsonBin::create_new_binary(ObIJsonBase *value, ObJsonBin *&new_bin) const
{
  INIT_SUCC(ret);
  common::ObIAllocator *allocator = NULL;
  void *buf = NULL;
  if (value != NULL) { // use stack memory
    buf = value;
    allocator = value->get_allocator();
  } else if (OB_ISNULL(allocator_)) { // check allocator_
    ret = OB_ERR_NULL_VALUE;
  } else { // use allocator_
    allocator = allocator_;
    buf = allocator->alloc(sizeof(ObJsonBin));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }   
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FALSE_IT(new_bin = new (buf) ObJsonBin(allocator))) {
  } else {
    ret = reset_child(*new_bin, meta_.raw_type_, pos_, meta_.entry_size_);
  }

  return ret;
}

int ObJsonBin::get_array_element(uint64_t index, ObIJsonBase *&value) const
{
  INIT_SUCC(ret);
  ObJsonBin *new_bin = NULL;

  if (OB_FAIL(check_valid_array_op(index))) {
  } else if (OB_FAIL(create_new_binary(value, new_bin))) {
  } else if (OB_FAIL(new_bin->element(index))) {
  } else {
    value = new_bin;
  }

  return ret;
}

int ObJsonBin::get_object_value(uint64_t index, ObIJsonBase *&value) const
{
  INIT_SUCC(ret);
  ObJsonBin *new_bin = NULL;

  if (OB_FAIL(check_valid_object_op(index))) {
  } else if (OB_FAIL(create_new_binary(value, new_bin))) {
  } else if (OB_FAIL(new_bin->element(index))) {
  } else {
    value = new_bin;
  }

  return ret;
}

int ObJsonBin::get_object_value(uint64_t index, ObString &key, ObIJsonBase *&value) const
{
  INIT_SUCC(ret);
  ObJsonBin *new_bin = NULL;

  if (OB_FAIL(check_valid_object_op(index))) {
  } else if (OB_FAIL(create_new_binary(value, new_bin))) {
  } else if (OB_FAIL(new_bin->get_key_in_object(index, key))) {
  } else if (OB_FAIL(new_bin->get_element_v0(index))) {
  } else {
    value = new_bin;
  }

  return ret;
}

int ObJsonBin::get_object_value(const ObString &key, ObIJsonBase *&value) const
{
  INIT_SUCC(ret);
  ObJsonBin *new_bin = NULL;

  if (json_type() != ObJsonNodeType::J_OBJECT) { // check json node type
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(create_new_binary(value, new_bin))) {
  } else {
    ret = new_bin->lookup(key);
    if (OB_SUCC(ret)) {
      value = new_bin;
    } else if (ret == OB_SEARCH_NOT_FOUND) {
    } else {
      LOG_WARN("fail to access key node for new jsonbin.", K(ret), K(key));
    }
  }

  return ret;
}


int ObJsonBin::get_used_bytes(uint64_t &size) const
{
  INIT_SUCC(ret);
  if (! is_at_root()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    size = cursor_->get_length();
  }
  return ret;
}


int ObJsonBin::get_raw_binary(ObString &buf, ObIAllocator *allocator) const
{
  if ((json_type() == ObJsonNodeType::J_ARRAY || json_type() == ObJsonNodeType::J_OBJECT)
      && get_update_ctx() == nullptr && is_at_root()) {
    return cursor_->get_data(buf);
  }
  json::BinarySource source(*cursor_);
  const json::StoredValueResult result = json::stored_document(
      source, pos_, meta_.raw_type_, meta_.entry_size_);
  int ret = stored_value_status(result.status, result.source_status);
  ObIAllocator *output_allocator = allocator == nullptr ? allocator_ : allocator;
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(output_allocator)) {
      ret = OB_ERR_NULL_VALUE;
    } else {
      ret = ob_write_string(*output_allocator, ObString(result.binary.size(),
          reinterpret_cast<const char *>(result.binary.data())), buf);
    }
  }
  return ret;
}

int ObJsonBin::get_free_space(size_t &space) const
{
  if (cursor_->get_length() == 0) { space = 0; return OB_SUCCESS; }
  if (!is_at_root()) { return OB_INVALID_ARGUMENT; }
  json::BinarySource source(*cursor_);
  const json::StoredValueResult result = json::stored_free_space(
      source, pos_, meta_.raw_type_, meta_.entry_size_);
  const int ret = stored_value_status(result.status, result.source_status);
  if (ret == OB_SUCCESS) { space = result.size; }
  return ret;
}

int ObJsonBin::init_cursor(const ObString& data)
{
  INIT_SUCC(ret);
  if (OB_NOT_NULL(ctx_) && (OB_NOT_NULL(ctx_->update_ctx_))) {
    if (! data.empty()) {
      ret = OB_ERR_UNEXPECTED;
    } else  if (OB_ISNULL(ctx_->update_ctx_->cursor_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      cursor_ = ctx_->update_ctx_->cursor_;
    }
  } else if (OB_FAIL(local_cursor_.init(data))) {
  } else {
    cursor_ = &local_cursor_;
  }
  return ret;
}

// reset iter to root
int ObJsonBin::reset_iter()
{
  return reset(0);
}

int ObJsonBin::reset(const uint8_t type, const int64_t offset, const uint8_t value_entry_var_type)
{
  INIT_SUCC(ret);
  if (OB_FAIL(reset(type, local_cursor_.data(), offset, value_entry_var_type, ctx_))) {
  }
  return ret;
}

int ObJsonBin::reset(const int64_t offset)
{
  INIT_SUCC(ret);
  if (OB_FAIL(reset(local_cursor_.data(), offset, ctx_))) {
  }
  return ret;
}

int ObJsonBin::reset_child(
    ObJsonBin &child,
    const uint8_t child_type,
    const int64_t child_offset,
    const uint8_t value_entry_var_type) const
{
  INIT_SUCC(ret);
  if (OB_FAIL(child.reset(child_type, local_cursor_.data(), child_offset, value_entry_var_type, ctx_))) {
  }
  return ret;
}

bool ObJsonBin::is_empty_data() const
{
  return nullptr == cursor_ || cursor_->get_length() <= 0;
}

int ObJsonBin::reset(const ObString &buffer, int64_t offset, ObJsonBinCtx *ctx)
{
  int ret = OB_SUCCESS;
  ctx_ = ctx;
  meta_ = Metadata{};
  if (OB_FAIL(init_cursor(buffer))) {
  } else if (is_empty_data()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    json::BinarySource source(*cursor_);
    const json::StoredLocationResult position = json::stored_position(source, offset);
    if (OB_FAIL(stored_value_status(position.status, position.source_status))) {
    } else {
      pos_ = position.offset;
      meta_.raw_type_ = position.raw_type;
      meta_.entry_size_ = position.entry_type;
      ret = init_bin_data();
    }
  }
  return ret;
}

int ObJsonBin::reset(
    const uint8_t type,
    const ObString &buffer,
    const int64_t offset,
    const uint8_t value_entry_var_type,
    ObJsonBinCtx *ctx)
{
  INIT_SUCC(ret);
  pos_ = offset;
  ctx_ = ctx;
  meta_ = Metadata{};
  meta_.raw_type_ = type;
  meta_.entry_size_ = value_entry_var_type;

  if (OB_FAIL(init_cursor(buffer))) {
  } else if (is_empty_data()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(init_bin_data())) {
  } else if (meta_.raw_type_ != type) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObJsonBin::init_bin_data()
{
  json::BinarySource source(*cursor_);
  const json::StoredMetadataResult info = json::stored_metadata(source, pos_, meta_.raw_type_, meta_.entry_size_);
  int ret = stored_value_status(info.status, info.source_status);
  ObString data;
  if (OB_SUCC(ret)) {
    meta_.bytes_ = info.encoded_size;
    meta_.element_count_ = info.count;
    meta_.value_type_ = static_cast<ObJsonNodeType>(info.value_type);
    root_pos_ = info.root_offset;
    if (info.has_header && ctx_ == nullptr) {
      if (allocator_ == nullptr) { return OB_ERR_UNEXPECTED; }
      if ((ctx_ = OB_NEWx(ObJsonBinCtx, allocator_)) == nullptr) {
        return OB_ALLOCATE_MEMORY_FAILED;
      }
      is_alloc_ctx_ = true;
    }
    if (ctx_ != nullptr) {
      ctx_->extend_seg_offset_ = info.extension;
    }
    meta_.entry_size_ = info.entry_type;
    switch (info.value_class) {
      case json::StoredValueClass::Null:
      case json::StoredValueClass::Container: break;
      case json::StoredValueClass::Int: int_val_ = info.int_value; break;
      case json::StoredValueClass::Uint:
      case json::StoredValueClass::Boolean: uint_val_ = info.uint_value; break;
      case json::StoredValueClass::Double: double_val_ = info.double_value; break;
      case json::StoredValueClass::Float: float_val_ = info.float_value; break;
      case json::StoredValueClass::Text:
      case json::StoredValueClass::Opaque: {
        meta_.str_data_offset_ = info.data_offset;
        meta_.field_type_ = static_cast<ObObjType>(info.field_type);
        ret = cursor_->get(pos_ + info.data_offset, info.count, data);
        break;
      }
      case json::StoredValueClass::Decimal: {
        int64_t position = pos_;
        ObPrecision precision = -1;
        ObScale scale = -1;
        if (OB_FAIL(cursor_->decode_i16(position, &precision))) {
        } else if (OB_FAIL(cursor_->decode_i16(position, &scale))) {
        } else if (OB_FAIL(cursor_->deserialize(position, &number_))) {
        } else {
          prec_ = precision;
          scale_ = scale;
        }
        break;
      }
      case json::StoredValueClass::Temporal: {
        int_val_ = info.int_value;
        meta_.field_type_ = ObJsonBaseUtil::get_time_type(json_type());
        break;
      }
      default: ret = OB_ERR_UNEXPECTED; break;
    }
  }
  return ret;
}

// move iter to element i without copy
int ObJsonBin::element(size_t index)
{
  INIT_SUCC(ret);
  ObJsonNodeType node_type = this->json_type();
  if (node_type != ObJsonNodeType::J_ARRAY && node_type != ObJsonNodeType::J_OBJECT) {
    ret = OB_OBJ_TYPE_ERROR;
  } else if (index >= element_count()) {
    ret = OB_OUT_OF_ELEMENT;
  } else {
    ret = get_element_v0(index);
    if (OB_FAIL(ret)) {
    }
  }
  return ret;
}

// get key id by key
int ObJsonBin::lookup_index(const ObString &key, size_t *idx) const
{
  int ret = OB_SUCCESS;
  if (json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_OBJ_TYPE_ERROR;
  } else {
    json::BinarySource source(*cursor_);
    const json::StoredLocationResult result = json::stored_find_key(source, pos_, meta_.raw_type_,
        meta_.entry_size_, rust::Slice<const uint8_t>(
        reinterpret_cast<const uint8_t *>(key.ptr()), key.length()));
    if (OB_FAIL(stored_value_status(result.status, result.source_status))) {
    } else if (!result.found) {
      ret = OB_SEARCH_NOT_FOUND;
    } else {
      *idx = result.index;
    }
  }
  return ret;
}

// mover iter to the value of key
int ObJsonBin::lookup(const ObString &key)
{
  INIT_SUCC(ret);
  ObJsonNodeType node_type = this->json_type();
  if (node_type != ObJsonNodeType::J_OBJECT) {
    ret = OB_OBJ_TYPE_ERROR;
  } else {
    size_t idx = 0;
    ret = lookup_index(key, &idx);
    if (ret == OB_SEARCH_NOT_FOUND) {
    } else if (OB_FAIL(ret)) {
    } else if (OB_FAIL(element(idx))) {
    }
  }
  return ret;
}

int ObJsonBin::native_child(size_t index, uint64_t &offset, uint8_t &type, uint8_t &entry_type) const
{
  json::BinarySource source(*cursor_);
  const json::StoredLocationResult child = json::stored_child(
      source, pos_, meta_.raw_type_, meta_.entry_size_, index);
  int ret = stored_value_status(child.status, child.source_status);
  if (OB_SUCC(ret)) {
    offset = child.offset;
    type = child.raw_type;
    entry_type = child.entry_type;
  }
  return ret;
}

int ObJsonBin::get_element_v0(size_t index)
{
  int ret = OB_SUCCESS;
  uint64_t offset = 0;
  uint8_t type = 0;
  uint8_t entry_type = 0;
  if (OB_FAIL(native_child(index, offset, type, entry_type))) {
  } else {
    ret = reset(type, offset, entry_type);
  }
  return ret;
}

int ObJsonBin::get_key_in_object(size_t index, ObString &key) const
{
  json::BinarySource source(*cursor_);
  const json::StoredLocationResult result = json::stored_key(
      source, pos_, meta_.raw_type_, meta_.entry_size_, index);
  int ret = stored_value_status(result.status, result.source_status);
  if (OB_SUCC(ret)) {
    ret = cursor_->get(result.offset, result.length, key);
  }
  return ret;
}

void ObJsonBin::destroy()
{
  if (OB_NOT_NULL(ctx_) && is_alloc_ctx_) {
    ctx_->~ObJsonBinCtx();
    allocator_->free(ctx_);
    ctx_ = nullptr;
  }
  local_cursor_.reset();
  cursor_ = &local_cursor_;
}

int ObJsonBin::reset()
{
  INIT_SUCC(ret);
  destroy();
  return ret;
}

uint32_t ObJsonBin::depth() const
{
  json::BinarySource source(*cursor_);
  const json::StoredValueResult result = json::stored_depth(source, pos_, meta_.raw_type_, meta_.entry_size_);
  return result.status == 0 ? static_cast<uint32_t>(result.size) : 1;
}

bool ObJsonVerType::is_opaque_or_string(ObJsonNodeType type)
{
  return (type == ObJsonNodeType::J_OPAQUE ||
          type == ObJsonNodeType::J_STRING ||
          type == ObJsonNodeType::J_OBINARY ||
          type == ObJsonNodeType::J_OOID ||
          type == ObJsonNodeType::J_ORAWHEX ||
          type == ObJsonNodeType::J_ORAWID ||
          type == ObJsonNodeType::J_ODAYSECOND ||
          type == ObJsonNodeType::J_OYEARMONTH);
}


int ObJsonBin::should_pack_diff(bool &is_should_pack) const
{
  INIT_SUCC(ret);
  ObJsonBinUpdateCtx *update = get_update_ctx();
  const uint64_t total = cursor_->get_length();
  const uint64_t extension = ctx_ == nullptr ? 0 : ctx_->extend_seg_offset_;
  if (total == 0 || extension == 0) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(update)) {
    is_should_pack = false;
  } else {
    ret = update->should_pack_diff(total, extension, pos_, is_should_pack);
  }
  return ret;
}

const char *ObJsonBin::get_data() const
{
  INIT_SUCC(ret);
  ObString data;
  if (OB_FAIL(cursor_->get(pos_ + meta_.str_data_offset_, element_count(), data))) {
  }
  return data.ptr();
}

ObJsonBinCtx::~ObJsonBinCtx()
{
  if (OB_NOT_NULL(update_ctx_) && is_update_ctx_alloc_) {
    update_ctx_->~ObJsonBinUpdateCtx();
    update_ctx_ = nullptr;
  }
}

} // namespace common
} // namespace oceanbase

