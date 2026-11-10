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
#include "storage/ob_storage_column_schema.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#include "share/datum/typed_datum_compare.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase { using namespace common; namespace storage {
namespace {
// The codec may allocate unpublished nested leaves before an error. Track
// allocations in the real caller owner so all of them roll back atomically.
class StorageColumnDecodeAllocator : public ObIAllocator
{
public:
  explicit StorageColumnDecodeAllocator(ObIAllocator &owner) : owner_(owner), committed_(false) {}
  ~StorageColumnDecodeAllocator()
  {
    if (!committed_) {
      for (int64_t i=0; i<allocations_.count(); ++i) { owner_.free(allocations_.at(i)); }
    }
  }
  void *alloc(int64_t size) override
  {
    void *ptr=owner_.alloc(size);
    if (ptr!=nullptr && allocations_.push_back(ptr)!=OB_SUCCESS) { owner_.free(ptr);ptr=nullptr; }
    return ptr;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *ptr) override
  {
    for (int64_t i=0; ptr!=nullptr && i<allocations_.count(); ++i) {
      if (allocations_.at(i)==ptr) { allocations_.remove(i);owner_.free(ptr);break; }
    }
  }
  void commit() { committed_=true; }
private:
  ObIAllocator &owner_;
  ObSEArray<void *,16> allocations_;
  bool committed_;
};
} // namespace

ObStorageColumnSchema::ObStorageColumnSchema() { reset(); }
ObStorageColumnSchema::~ObStorageColumnSchema() {}
void ObStorageColumnSchema::reset()
{
  info_=0;
  default_checksum_=0;
  meta_type_.reset();
  orig_default_value_.reset();
  orig_default_meta_.reset();
}
void ObStorageColumnSchema::clear_default_value(ObIAllocator &allocator)
{
  DatumParameterCodec::release_owned_schema_native_value(orig_default_value_,orig_default_meta_,allocator);
  orig_default_meta_.reset();
}
void ObStorageColumnSchema::destroy(ObIAllocator &allocator)
{
  clear_default_value(allocator);
  reset();
}
bool ObStorageColumnSchema::is_valid() const
{
  return reserved_==0 && is_valid_sql_value_type(static_cast<SqlValueType>(meta_type_.get_type()));
}
int ObStorageColumnSchema::deep_copy_default_val(ObIAllocator &allocator,const DatumValue &value)
{
  Datum owned;
  SqlTypeMeta meta=value.meta_;
  if (value.is_null() && !meta.is_null()) { meta.set_null(); }
  const int ret=DatumParameterCodec::clone_schema_native_value(value.datum_,meta,allocator,owned);
  if (ret==OB_SUCCESS) {
    clear_default_value(allocator);
    orig_default_value_=owned;
    orig_default_meta_=meta;
  }
  return ret;
}
int ObStorageColumnSchema::deep_copy_default_datum(ObIAllocator &allocator,const Datum &value,
                                                 const SqlTypeMeta &source_meta)
{
  Datum owned;
  SqlTypeMeta meta=source_meta;
  if (value.is_null() && !meta.is_null()) { meta.set_null(); }
  const int ret=DatumParameterCodec::clone_schema_native_value(value,meta,allocator,owned);
  if (ret==OB_SUCCESS) {
    clear_default_value(allocator);
    orig_default_value_=owned;
    orig_default_meta_=meta;
  }
  return ret;
}
int ObStorageColumnSchema::export_orig_default_value(DatumValue &output,void *scratch,int64_t capacity) const
{
  ObDatum view;
  int ret=DatumParameterCodec::export_schema_legacy_value(
      orig_default_value_,orig_default_meta_,view,scratch,capacity);
  if (ret==OB_SUCCESS) { ret=output.assign_legacy_borrowed(view,orig_default_meta_); }
  return ret;
}
int ObStorageColumnSchema::copy_orig_default_value(DatumValue &output,ObIAllocator &owner) const
{
  alignas(8) char scratch[64];
  DatumValue view;
  const int ret=export_orig_default_value(view,scratch,sizeof(scratch));
  return ret==OB_SUCCESS ? clone_typed_value(view,output,owner) : ret;
}

OB_DEF_SERIALIZE(ObStorageColumnSchema)
{
  int ret=OB_SUCCESS;
  OB_UNIS_ENCODE(info_);
  OB_UNIS_ENCODE(default_checksum_);
  OB_UNIS_ENCODE(meta_type_);
  if (OB_SUCC(ret)) {
    ret=DatumParameterCodec::encode_schema_native_value(orig_default_value_,orig_default_meta_,buf,buf_len,pos);
  }
  return ret;
}
OB_DEF_SERIALIZE_SIZE(ObStorageColumnSchema)
{
  int64_t len=0,value_len=0;
  OB_UNIS_ADD_LEN(info_);
  OB_UNIS_ADD_LEN(default_checksum_);
  OB_UNIS_ADD_LEN(meta_type_);
  if (DatumParameterCodec::schema_native_value_size(orig_default_value_,orig_default_meta_,value_len)==OB_SUCCESS) {
    len+=value_len;
  }
  return len;
}
int ObStorageColumnSchema::deserialize(ObIAllocator &allocator,const char *buf,const int64_t data_len,int64_t &pos)
{
  int ret=OB_SUCCESS;
  int64_t version=0,len=0;
  OB_UNIS_DECODE(version);
  OB_UNIS_DECODE(len);
  CHECK_VERSION_LENGTH(ObStorageColumnSchema,version,len);
  if (OB_SUCC(ret)) {
    const int64_t original=pos;
    pos=0;
    ret=decode_fields(allocator,buf+original,len,pos);
    pos=original+len;
  }
  return ret;
}
int ObStorageColumnSchema::decode_fields(ObIAllocator &allocator,const char *buf,const int64_t data_len,int64_t &pos)
{
  int ret=OB_SUCCESS;
  StorageColumnDecodeAllocator allocations(allocator);
  ObStorageColumnSchema parsed;
  OB_UNIS_DECODE(parsed.info_);
  OB_UNIS_DECODE(parsed.default_checksum_);
  OB_UNIS_DECODE(parsed.meta_type_);
  if (OB_SUCC(ret) && pos<data_len) {
    ret=DatumParameterCodec::decode_unis_native_value(buf,data_len,pos,allocations,
        parsed.orig_default_value_,parsed.orig_default_meta_);
  }
  if (OB_SUCC(ret)) {
    clear_default_value(allocator);
    info_=parsed.info_;
    default_checksum_=parsed.default_checksum_;
    meta_type_=parsed.meta_type_;
    orig_default_value_=parsed.orig_default_value_;
    orig_default_meta_=parsed.orig_default_meta_;
    parsed.orig_default_value_.reset();
    parsed.orig_default_meta_.reset();
    allocations.commit();
  }
  return ret;
}
} }
