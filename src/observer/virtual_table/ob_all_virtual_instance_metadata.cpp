/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#define USING_LOG_PREFIX SERVER

#include "observer/virtual_table/ob_all_virtual_instance_metadata.h"
#include "lib/time/ob_time_utility.h"
#include "share/instance_meta/instance_meta_key_codec.h"
#include "share/instance_meta/instance_meta_value_codec.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using share::instance_meta::MetaCollection;
using share::instance_meta::InstanceMetaKeyCodec;
using share::instance_meta::InstanceMetaValueCodec;
using share::instance_meta::ValueFormat;

InstanceMetadataTable::InstanceMetadataTable(storage::InstanceMetaStore &store)
    : store_(store), tx_(), started_(false), has_lower_(false), collection_id_(1),
      last_key_(), key_json_(), key_hex_(), value_json_(), value_base64_(),
      value_size_(0), value_is_json_(false)
{}

InstanceMetadataTable::~InstanceMetadataTable() { reset(); }

void InstanceMetadataTable::reset()
{
  if (tx_.is_active()) { (void)store_.rollback(tx_); }
  started_ = false;
  has_lower_ = false;
  collection_id_ = 1;
  last_key_.clear();
  key_json_.clear();
  key_hex_.clear();
  value_json_.clear();
  value_base64_.clear();
  value_size_ = 0;
  value_is_json_ = false;
  ObVirtualTableScannerIterator::reset();
}

bool InstanceMetadataTable::has_column(uint64_t id) const
{
  bool found = false;
  for (int64_t i = 0; !found && i < output_column_ids_.count(); ++i) {
    found = output_column_ids_.at(i) == id;
  }
  return found;
}

void InstanceMetadataTable::set_text(ObObj &cell, const std::string &value, bool lob) const
{
  if (lob) { cell.set_lob_value(ObLongTextType, value.data(), value.size()); }
  else { cell.set_varchar(value.data(), value.size()); }
  cell.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
}

int InstanceMetadataTable::load_next()
{
  int ret = OB_SUCCESS;
  if (!started_) {
    started_ = true;
    ret = store_.begin(tx_, ObTimeUtility::current_time() + 120 * 1000 * 1000, true);
  }
  while (ret == OB_SUCCESS && collection_id_ <= 8) {
    const MetaCollection collection = static_cast<MetaCollection>(collection_id_);
    const auto *desc = InstanceMetaKeyCodec::describe(collection);
    if (desc == nullptr) { ++collection_id_; continue; }
    // Keep the bound stable while the callback advances last_key_.
    const std::string lower_key = last_key_;
    storage::InstanceMetaStore::KeyRange range;
    if (has_lower_) {
      range.lower = ObString(lower_key.size(), lower_key.data());
      range.has_lower = true;
      range.include_lower = false;
    }
    bool found = false;
    ret = store_.scan(tx_, collection, range,
        [&](const ObString &key, const ObString &value, bool &stop) {
          ValueFormat format;
          ObString payload;
          int row_ret = InstanceMetaKeyCodec::decode_json(collection, key, key_json_);
          if (row_ret == OB_SUCCESS) {
            row_ret = InstanceMetaValueCodec::decode(value, format, payload);
          }
          if (row_ret == OB_SUCCESS) {
            last_key_.assign(key.ptr(), key.length());
            has_lower_ = true;
            value_size_ = payload.length();
            value_is_json_ = format == ValueFormat::JSON;
            value_json_.clear();
            value_base64_.clear();
            if (value_is_json_) {
              value_json_.assign(payload.ptr(), payload.length());
            } else if (has_column(VALUE_BASE64)) {
              InstanceMetaValueCodec::base64(payload, value_base64_);
            }
            if (has_column(KEY_HEX)) {
              static const char hex[] = "0123456789abcdef";
              key_hex_.clear();
              key_hex_.reserve(key.length() * 2);
              for (int64_t i = 0; i < key.length(); ++i) {
                const unsigned char byte = static_cast<unsigned char>(key.ptr()[i]);
                key_hex_ += hex[byte >> 4];
                key_hex_ += hex[byte & 15];
              }
            }
            found = true;
            stop = true;
          }
          return row_ret;
        });
    if (ret == OB_SUCCESS && found) { break; }
    if (ret == OB_SUCCESS) {
      ++collection_id_;
      has_lower_ = false;
      last_key_.clear();
    }
  }
  if (ret == OB_SUCCESS && collection_id_ > 8) {
    if (tx_.is_active()) { (void)store_.rollback(tx_); }
    ret = OB_ITER_END;
  }
  return ret;
}

int InstanceMetadataTable::fill_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  const auto *desc = InstanceMetaKeyCodec::describe(static_cast<MetaCollection>(collection_id_));
  if (desc == nullptr || cur_row_.cells_ == nullptr) { return OB_ERR_UNEXPECTED; }
  for (int64_t i = 0; ret == OB_SUCCESS && i < output_column_ids_.count(); ++i) {
    ObObj &cell = cur_row_.cells_[i];
    switch (output_column_ids_.at(i)) {
      case COLLECTION_ID: cell.set_uint64(collection_id_); break;
      case COLLECTION_NAME:
        cell.set_varchar(desc->name);
        cell.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      case KEY_JSON: set_text(cell, key_json_, true); break;
      case KEY_HEX: set_text(cell, key_hex_, true); break;
      case VALUE_FORMAT:
        cell.set_varchar(value_is_json_ ? "JSON" : "BYTES");
        cell.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      case VALUE_JSON:
        if (value_is_json_) { set_text(cell, value_json_, true); }
        else { cell.set_null(); }
        break;
      case VALUE_SIZE: cell.set_int(value_size_); break;
      case VALUE_BASE64:
        if (value_is_json_) { cell.set_null(); }
        else { set_text(cell, value_base64_, true); }
        break;
      default: ret = OB_ERR_UNEXPECTED; break;
    }
  }
  if (ret == OB_SUCCESS) { row = &cur_row_; }
  return ret;
}

int InstanceMetadataTable::inner_get_next_row(ObNewRow *&row)
{
  int ret = load_next();
  if (ret == OB_SUCCESS) { ret = fill_row(row); }
  return ret;
}

} // namespace observer
} // namespace oceanbase
