/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_INSTANCE_METADATA_H_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_INSTANCE_METADATA_H_

#include <string>
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "storage/instance_meta/instance_meta_store.h"

namespace oceanbase
{
namespace observer
{

class InstanceMetadataTable final : public common::ObVirtualTableScannerIterator
{
public:
  explicit InstanceMetadataTable(storage::InstanceMetaStore &store);
  ~InstanceMetadataTable() override;
  void reset() override;
  int inner_get_next_row(common::ObNewRow *&row) override;

private:
  enum ColumnId
  {
    COLLECTION_ID = common::OB_APP_MIN_COLUMN_ID,
    COLLECTION_NAME,
    KEY_JSON,
    KEY_HEX,
    VALUE_FORMAT,
    VALUE_JSON,
    VALUE_SIZE,
    VALUE_BASE64,
  };

  int load_next();
  int fill_row(common::ObNewRow *&row);
  bool has_column(uint64_t id) const;
  void set_text(common::ObObj &cell, const std::string &value, bool lob) const;

  storage::InstanceMetaStore &store_;
  storage::InstanceMetaStore::Transaction tx_;
  bool started_;
  bool has_lower_;
  uint64_t collection_id_;
  std::string last_key_;
  std::string key_json_;
  std::string key_hex_;
  std::string value_json_;
  std::string value_base64_;
  int64_t value_size_;
  bool value_is_json_;
};

} // namespace observer
} // namespace oceanbase
#endif
