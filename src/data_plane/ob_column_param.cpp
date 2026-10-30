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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "data_plane/access/ob_table_param.h"
#include "share/schema/ob_column_schema.h"
#include "share/datum/typed_datum_compare.h"
#include "common/datum/datum_parameter_codec.h"
#include "lib/container/ob_se_array.h"
namespace oceanbase { using namespace common; namespace share { namespace schema {
namespace {
// Values are allocated in the caller's allocator. Keep only pointer records
// locally, so a failed decode can reclaim even allocations not yet published
// by the value codec. The common two-scalar case needs no tracking allocation.
class ColumnParamDecodeAllocator : public ObIAllocator
{
public:
  explicit ColumnParamDecodeAllocator(ObIAllocator &owner) : owner_(owner), committed_(false) {}
  ~ColumnParamDecodeAllocator() {
    if (!committed_) { for (int64_t i = 0; i < allocations_.count(); ++i) { owner_.free(allocations_.at(i)); } }
  }
  void *alloc(int64_t size) override {
    void *memory = owner_.alloc(size);
    if (memory != nullptr && allocations_.push_back(memory) != OB_SUCCESS) {
      owner_.free(memory); memory = nullptr;
    }
    return memory;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *memory) override {
    if (memory != nullptr) {
      for (int64_t i = 0; i < allocations_.count(); ++i) {
        if (allocations_.at(i) == memory) { allocations_.remove(i); owner_.free(memory); return; }
      }
    }
  }
  void commit() { committed_ = true; }
private:
  ObIAllocator &owner_;
  bool committed_;
  ObSEArray<void *, 16> allocations_;
};
}

ObColumnParam::ObColumnParam(ObIAllocator &allocator) : allocator_(allocator) { reset(); }
ObColumnParam::~ObColumnParam() {}

void ObColumnParam::release_default_value(DatumValue &value)
{
  DatumParameter owned;
  owned.assign_value(value);
  DatumParameterArray::release_owned(owned, allocator_);
  value.reset();
}

void ObColumnParam::reset()
{
  release_default_value(orig_default_value_);
  release_default_value(cur_default_value_);
  column_id_ = OB_INVALID_ID;
  meta_type_.reset();
  order_ = ObOrderType::ASC;
  accuracy_.reset();
  is_nullable_for_write_ = false;
  is_nullable_for_read_ = false;
  is_gen_col_ = false;
  is_virtual_gen_col_ = false;
  is_gen_col_udf_expr_ = false;
  is_hidden_ = false;
  lob_chunk_size_ = OB_DEFAULT_LOB_CHUNK_SIZE;
  is_data_table_rowkey_ = false;
}
void ObColumnParam::destroy() { reset(); }

int ObColumnParam::copy_default_value(const DatumValue &source, DatumValue &target)
{
  DatumValue input = source, owned;
  if (input.is_null() && !input.meta_.is_null()) { input.meta_.set_null(); }
  const int ret = clone_typed_value(input, owned, allocator_);
  if (ret == OB_SUCCESS) { release_default_value(target); target = owned; }
  return ret;
}
int ObColumnParam::copy_default_value(const DatumParameter &source, DatumValue &target)
{
  return copy_default_value(source.value_view(), target);
}
int32_t ObColumnParam::get_data_length() const
{
  return ObColumnSchemaV2::get_data_length(accuracy_, meta_type_);
}

void ObColumnParam::move_fields_from(ObColumnParam &source)
{
  reset();
  column_id_ = source.column_id_;
  meta_type_ = source.meta_type_;
  order_ = source.order_;
  accuracy_ = source.accuracy_;
  is_nullable_for_write_ = source.is_nullable_for_write_;
  is_nullable_for_read_ = source.is_nullable_for_read_;
  is_gen_col_ = source.is_gen_col_;
  is_virtual_gen_col_ = source.is_virtual_gen_col_;
  is_gen_col_udf_expr_ = source.is_gen_col_udf_expr_;
  is_hidden_ = source.is_hidden_;
  lob_chunk_size_ = source.lob_chunk_size_;
  is_data_table_rowkey_ = source.is_data_table_rowkey_;
  orig_default_value_ = source.orig_default_value_;
  cur_default_value_ = source.cur_default_value_;
  source.orig_default_value_.reset();
  source.cur_default_value_.reset();
}
int ObColumnParam::assign(const ObColumnParam &other)
{
  int ret = OB_SUCCESS;
  if (&other != this) {
    ObColumnParam copy(allocator_);
    copy.column_id_ = other.column_id_;
    copy.meta_type_ = other.meta_type_;
    copy.order_ = other.order_;
    copy.accuracy_ = other.accuracy_;
    copy.is_nullable_for_write_ = other.is_nullable_for_write_;
    copy.is_nullable_for_read_ = other.is_nullable_for_read_;
    copy.is_gen_col_ = other.is_gen_col_;
    copy.is_virtual_gen_col_ = other.is_virtual_gen_col_;
    copy.is_gen_col_udf_expr_ = other.is_gen_col_udf_expr_;
    copy.is_hidden_ = other.is_hidden_;
    copy.lob_chunk_size_ = other.lob_chunk_size_;
    copy.is_data_table_rowkey_ = other.is_data_table_rowkey_;
    if (OB_FAIL(copy.set_orig_default_value(other.orig_default_value_))) {
    } else if (OB_FAIL(copy.set_cur_default_value(other.cur_default_value_))) {
    } else {
      move_fields_from(copy);
    }
    copy.destroy();
  }
  return ret;
}

OB_DEF_SERIALIZE(ObColumnParam)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, column_id_, meta_type_, accuracy_);
  if (OB_SUCC(ret)) { ret = DatumParameterCodec::encode_value(orig_default_value_.datum_, orig_default_value_.meta_, buf, buf_len, pos); }
  if (OB_SUCC(ret)) { ret = DatumParameterCodec::encode_value(cur_default_value_.datum_, cur_default_value_.meta_, buf, buf_len, pos); }
  LST_DO_CODE(OB_UNIS_ENCODE, order_, is_nullable_for_write_, is_gen_col_, is_virtual_gen_col_,
              is_gen_col_udf_expr_, is_nullable_for_read_, is_hidden_, lob_chunk_size_, is_data_table_rowkey_);
  return ret;
}
int ObColumnParam::decode_fields(const char *buf, int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, column_id_, meta_type_, accuracy_);
  if (OB_SUCC(ret) && pos < data_len) {
    ret = DatumParameterCodec::decode_unis_value(buf, data_len, pos, allocator_, orig_default_value_.datum_, orig_default_value_.meta_);
  }
  if (OB_SUCC(ret) && pos < data_len) {
    ret = DatumParameterCodec::decode_unis_value(buf, data_len, pos, allocator_, cur_default_value_.datum_, cur_default_value_.meta_);
  }
  OB_UNIS_DECODE(order_);
  if (OB_SUCC(ret)) {
    if (pos < data_len) {
      ret = serialization::decode(buf, data_len, pos, is_nullable_for_write_);
    } else { is_nullable_for_write_ = false; }
  }
  LST_DO_CODE(OB_UNIS_DECODE, is_gen_col_, is_virtual_gen_col_, is_gen_col_udf_expr_, is_nullable_for_read_, is_hidden_, lob_chunk_size_, is_data_table_rowkey_);
  return ret;
}
OB_DEF_DESERIALIZE(ObColumnParam)
{
  ColumnParamDecodeAllocator allocations(allocator_);
  ObColumnParam parsed(allocations);
  const int ret = parsed.decode_fields(buf, data_len, pos);
  if (ret == OB_SUCCESS) { move_fields_from(parsed); allocations.commit(); }
  return ret;
}
OB_DEF_SERIALIZE_SIZE(ObColumnParam)
{
  int64_t len = 0, size = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, column_id_, meta_type_, accuracy_);
  if (DatumParameterCodec::value_size(orig_default_value_.datum_, orig_default_value_.meta_, size) != OB_SUCCESS) { return 0; }
  len += size;
  if (DatumParameterCodec::value_size(cur_default_value_.datum_, cur_default_value_.meta_, size) != OB_SUCCESS) { return 0; }
  len += size;
  LST_DO_CODE(OB_UNIS_ADD_LEN, order_, is_nullable_for_write_, is_nullable_for_read_, is_gen_col_,
              is_virtual_gen_col_, is_gen_col_udf_expr_, is_hidden_, lob_chunk_size_, is_data_table_rowkey_);
  return len;
}
} } }
