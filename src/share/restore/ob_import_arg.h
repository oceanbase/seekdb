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

#ifndef OCEANBASE_SHARE_IMPORT_ARG_H
#define OCEANBASE_SHARE_IMPORT_ARG_H

#include "share/restore/ob_import_table_arg.h"
#include "share/restore/ob_import_remap_arg.h"

namespace oceanbase
{
namespace share
{

class ObImportArg final
{
  OB_UNIS_VERSION(1);
public:
  ObImportArg() : import_table_arg_(), remap_table_arg_() 
  {}

  const ObImportTableArg &get_import_table_arg() const { return import_table_arg_; }
  const ObImportRemapArg &get_remap_table_arg() const { return remap_table_arg_; }
  ObImportTableArg &get_import_table_arg() { return import_table_arg_; }
  ObImportRemapArg &get_remap_table_arg() { return remap_table_arg_; }
  
  const ObImportDatabaseArray &get_import_database_array() const;
  const ObImportTableArray &get_import_table_array() const;
  const ObImportPartitionArray &get_import_partition_array() const;

  const ObRemapDatabaseArray &get_remap_database_array() const;
  const ObRemapTableArray &get_remap_table_array() const;
  const ObRemapPartitionArray &get_remap_partition_array() const;
  const ObRemapTablegroupArray &get_remap_tablegroup_array() const;
  const ObRemapTablespaceArray &get_remap_tablespace_array() const;

  void reset();
  int assign(const ObImportArg &other);
  int add_import_database(const ObImportDatabaseItem &item);
  int add_import_table(const ObImportTableItem &item);
  int add_import_parition(const ObImportPartitionItem &item);
  int add_remap_database(const ObRemapDatabaseItem &item);
  int add_remap_table(const ObRemapTableItem &item);
  int add_remap_parition(const ObRemapPartitionItem &item);
  int add_remap_tablegroup(const ObRemapTablegroupItem &item);
  int add_remap_tablespace(const ObRemapTablespaceItem &item);

  TO_STRING_KV(K_(import_table_arg), K_(remap_table_arg));

private:
  ObImportTableArg import_table_arg_;
  ObImportRemapArg remap_table_arg_;
  DISALLOW_COPY_AND_ASSIGN(ObImportArg);
};


}
}

#endif
