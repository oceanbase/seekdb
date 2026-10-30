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
}
void ObStorageColumnSchema::clear_default_value(ObIAllocator &allocator)
{
  DatumParameter owned;
  owned.assign_value(orig_default_value_);
  DatumParameterArray::release_owned(owned,allocator);
  orig_default_value_.reset();
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
  DatumValue input=value,owned;
  if (input.is_null() && !input.meta_.is_null()) { input.meta_.set_null(); }
  const int ret=clone_typed_value(input,owned,allocator);
  if (ret==OB_SUCCESS) { clear_default_value(allocator);orig_default_value_=owned; }
  return ret;
}

OB_DEF_SERIALIZE(ObStorageColumnSchema)
{
  int ret=OB_SUCCESS;
  OB_UNIS_ENCODE(info_);
  OB_UNIS_ENCODE(default_checksum_);
  OB_UNIS_ENCODE(meta_type_);
  if (OB_SUCC(ret)) {
    ret=DatumParameterCodec::encode_value(orig_default_value_.datum_,orig_default_value_.meta_,buf,buf_len,pos);
  }
  return ret;
}
OB_DEF_SERIALIZE_SIZE(ObStorageColumnSchema)
{
  int64_t len=0,value_len=0;
  OB_UNIS_ADD_LEN(info_);
  OB_UNIS_ADD_LEN(default_checksum_);
  OB_UNIS_ADD_LEN(meta_type_);
  if (DatumParameterCodec::value_size(orig_default_value_.datum_,orig_default_value_.meta_,value_len)==OB_SUCCESS) {
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
    ret=DatumParameterCodec::decode_unis_value(buf,data_len,pos,allocations,
        parsed.orig_default_value_.datum_,parsed.orig_default_value_.meta_);
  }
  if (OB_SUCC(ret)) {
    clear_default_value(allocator);
    info_=parsed.info_;
    default_checksum_=parsed.default_checksum_;
    meta_type_=parsed.meta_type_;
    orig_default_value_=parsed.orig_default_value_;
    parsed.orig_default_value_.reset();
    allocations.commit();
  }
  return ret;
}
} }
