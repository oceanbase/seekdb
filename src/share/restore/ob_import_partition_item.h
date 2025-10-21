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

#ifndef OCEANBASE_SHARE_IMPORT_PARTITION_ITEM_H
#define OCEANBASE_SHARE_IMPORT_PARTITION_ITEM_H

#include "share/restore/ob_import_schema_item.h"

namespace oceanbase
{
namespace share
{
struct ObImportPartitionItem final : public ObIImportSchemaItem
{
  OB_UNIS_VERSION(1);
public:
  ObImportPartitionItem() : 
      ObIImportSchemaItem(ItemType::PARTITION),
      database_name_(),
      table_name_(),
      partition_name_()
  {}

  ObImportPartitionItem(common::ObNameCaseMode mode, const char *db_name, const int64_t db_len,
      const char *table_name, const int64_t table_len, const char *part_name, const int64_t part_len) :
      ObIImportSchemaItem(ItemType::PARTITION, mode),
      database_name_(db_len, db_name),
      table_name_(table_len, table_name),
      partition_name_(part_len, part_name)
  {}

  virtual void reset() override;
  virtual bool is_valid() const override;
  // ignore case
  virtual bool case_mode_equal(const ObIImportItem &other) const override;
  virtual int64_t get_format_serialize_size() const override;
  virtual int format_serialize(
      char *buf, 
      const int64_t buf_len, 
      int64_t &pos) const override;

  virtual int deep_copy(common::ObIAllocator &allocator, const ObIImportItem &src) override;
  int assign(const ObImportPartitionItem &other);

  TO_STRING_KV(K_(mode), K_(database_name), K_(table_name), K_(partition_name));

public:
  // The following 3 names are all c_style string, and '\0' is not included
  // into the length.
  common::ObString database_name_;
  common::ObString table_name_;
  common::ObString partition_name_;
};


using ObImportPartitionArray = ObImportSchemaItemArray<ObImportPartitionItem>;


}
}
#endif
