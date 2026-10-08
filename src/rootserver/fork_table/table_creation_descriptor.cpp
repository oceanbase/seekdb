/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "rootserver/fork_table/table_creation_descriptor.h"

namespace oceanbase {
namespace rootserver {
using namespace common;

bool TableCreationDescriptor::is_valid() const
{
  return schema_.is_valid() && extra_.data_format_version_ > 0;
}

void TableCreationDescriptor::reset()
{
  schema_.reset();
  extra_.reset();
  allocator_.reset();
  data_table_id_ = lob_meta_table_id_ = lob_piece_table_id_ = OB_INVALID_ID;
}

int TableCreationDescriptor::init(const share::schema::ObTableSchema &schema,
    uint64_t data_format_version)
{
  reset();
  int ret = schema_.init(allocator_, schema, false);
  if (ret == OB_SUCCESS) {
    ret = extra_.init(data_format_version, false, schema.get_micro_index_clustered());
  }
  if (ret == OB_SUCCESS) {
    data_table_id_ = schema.get_data_table_id();
    lob_meta_table_id_ = schema.get_aux_lob_meta_tid();
    lob_piece_table_id_ = schema.get_aux_lob_piece_tid();
    if (!is_valid()) { ret = OB_INVALID_ARGUMENT; }
  }
  if (ret != OB_SUCCESS) { reset(); }
  return ret;
}

int TableCreationDescriptor::encode(std::string &bytes) const
{
  if (!is_valid()) { return OB_NOT_INIT; }
  const int64_t magic = 0x5441424352454154LL; // TABCREAT
  const int64_t size = 16 + serialization::encoded_length(data_table_id_)
      + serialization::encoded_length(lob_meta_table_id_)
      + serialization::encoded_length(lob_piece_table_id_)
      + extra_.get_serialize_size() + schema_.get_serialize_size();
  std::string encoded(size, '\0');
  int64_t pos = 0;
  int ret = serialization::encode_i64(&encoded[0], size, pos, magic);
  if (ret == OB_SUCCESS) { ret = serialization::encode_i64(&encoded[0], size, pos, size); }
  if (ret == OB_SUCCESS) { ret = serialization::encode(&encoded[0], size, pos, data_table_id_); }
  if (ret == OB_SUCCESS) { ret = serialization::encode(&encoded[0], size, pos, lob_meta_table_id_); }
  if (ret == OB_SUCCESS) { ret = serialization::encode(&encoded[0], size, pos, lob_piece_table_id_); }
  if (ret == OB_SUCCESS) { ret = extra_.serialize(&encoded[0], size, pos); }
  if (ret == OB_SUCCESS) { ret = schema_.serialize(&encoded[0], size, pos); }
  if (ret == OB_SUCCESS && pos != size) { ret = OB_ERR_UNEXPECTED; }
  if (ret == OB_SUCCESS) { bytes.swap(encoded); }
  return ret;
}

int TableCreationDescriptor::decode(const std::string &bytes)
{
  reset();
  int64_t magic = 0, encoded_size = 0, pos = 0;
  const int64_t size = bytes.size();
  int ret = serialization::decode_i64(bytes.data(), size, pos, &magic);
  if (ret == OB_SUCCESS && magic != 0x5441424352454154LL) { ret = OB_DESERIALIZE_ERROR; }
  if (ret == OB_SUCCESS) { ret = serialization::decode_i64(bytes.data(), size, pos, &encoded_size); }
  if (ret == OB_SUCCESS && encoded_size != size) { ret = OB_DESERIALIZE_ERROR; }
  if (ret == OB_SUCCESS) { ret = serialization::decode(bytes.data(), size, pos, data_table_id_); }
  if (ret == OB_SUCCESS) { ret = serialization::decode(bytes.data(), size, pos, lob_meta_table_id_); }
  if (ret == OB_SUCCESS) { ret = serialization::decode(bytes.data(), size, pos, lob_piece_table_id_); }
  if (ret == OB_SUCCESS) { ret = extra_.deserialize(bytes.data(), size, pos); }
  if (ret == OB_SUCCESS) { ret = schema_.deserialize(allocator_, bytes.data(), size, pos); }
  if (ret == OB_SUCCESS && (pos != size || !is_valid())) { ret = OB_DESERIALIZE_ERROR; }
  if (ret != OB_SUCCESS) { reset(); }
  return ret;
}

int TableCreationDescriptor::append_to(obcall::ObBatchCreateTabletArg &batch,
    int64_t &index) const
{
  if (!is_valid() || !batch.is_inited() || !batch.table_schemas_.empty()
      || batch.create_tablet_schemas_.count() != batch.tablet_extra_infos_.count()) {
    return OB_INVALID_ARGUMENT;
  }
  void *memory = batch.allocator_.alloc(sizeof(storage::ObCreateTabletSchema));
  if (memory == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
  auto *copy = new (memory) storage::ObCreateTabletSchema();
  int ret = copy->init(batch.allocator_, schema_);
  if (ret == OB_SUCCESS) { ret = batch.create_tablet_schemas_.push_back(copy); }
  if (ret == OB_SUCCESS) {
    ret = batch.tablet_extra_infos_.push_back(extra_);
    if (ret != OB_SUCCESS) { batch.create_tablet_schemas_.pop_back(); }
  }
  if (ret == OB_SUCCESS) { index = batch.create_tablet_schemas_.count() - 1; }
  else { copy->~ObCreateTabletSchema(); batch.allocator_.free(memory); }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
