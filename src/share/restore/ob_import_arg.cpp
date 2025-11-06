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
#define USING_LOG_PREFIX SHARE
#include "share/restore/ob_import_arg.h"

namespace oceanbase
{
namespace share
{

// ObImportArg
OB_SERIALIZE_MEMBER(ObImportArg, import_table_arg_, remap_table_arg_);

const ObImportDatabaseArray &ObImportArg::get_import_database_array() const
{
  return import_table_arg_.get_import_database_array();
}

const ObImportTableArray &ObImportArg::get_import_table_array() const
{
  return import_table_arg_.get_import_table_array();
}

const ObImportPartitionArray &ObImportArg::get_import_partition_array() const
{
  return import_table_arg_.get_import_partition_array();
}

const ObRemapDatabaseArray &ObImportArg::get_remap_database_array() const
{
  return remap_table_arg_.get_remap_database_array();
}

const ObRemapTableArray &ObImportArg::get_remap_table_array() const
{
  return remap_table_arg_.get_remap_table_array();
}

const ObRemapPartitionArray &ObImportArg::get_remap_partition_array() const
{
  return remap_table_arg_.get_remap_partition_array();
}

const ObRemapTablegroupArray &ObImportArg::get_remap_tablegroup_array() const
{
  return remap_table_arg_.get_remap_tablegroup_array();
}

const ObRemapTablespaceArray &ObImportArg::get_remap_tablespace_array() const
{
  return remap_table_arg_.get_remap_tablespace_array();
}

void ObImportArg::reset()
{
  import_table_arg_.reset();
  remap_table_arg_.reset();
}

int ObImportArg::assign(const ObImportArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(import_table_arg_.assign(other.get_import_table_arg()))) {
    LOG_WARN("failed to assign import table arg", K(ret), "import table arg", other.get_import_table_arg());
  } else if (OB_FAIL(remap_table_arg_.assign(other.get_remap_table_arg()))) {
    LOG_WARN("failed to assign remap table arg", K(ret), "remap table arg", other.get_remap_table_arg());
  }
  return ret;
}

int ObImportArg::add_import_database(const ObImportDatabaseItem &item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(import_table_arg_.add_database(item))) {
    LOG_WARN("failed to add import database", K(ret), K(item));
  }
  return ret;
}

int ObImportArg::add_import_table(const ObImportTableItem &item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(import_table_arg_.add_table(item))) {
    LOG_WARN("failed to add import table", K(ret), K(item));
  }
  return ret;
}

int ObImportArg::add_import_parition(const ObImportPartitionItem &item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(import_table_arg_.add_partition(item))) {
    LOG_WARN("failed to add import partition", K(ret), K(item));
  }
  return ret;
}

int ObImportArg::add_remap_database(const ObRemapDatabaseItem &item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(remap_table_arg_.add_remap_database(item))) {
    LOG_WARN("failed to add remap database", K(ret), K(item));
  }
  return ret;
}

int ObImportArg::add_remap_table(const ObRemapTableItem &item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(remap_table_arg_.add_remap_table(item))) {
    LOG_WARN("failed to add remap table", K(ret), K(item));
  }
  return ret;
}

int ObImportArg::add_remap_parition(const ObRemapPartitionItem &item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(remap_table_arg_.add_remap_parition(item))) {
    LOG_WARN("failed to add remap partition", K(ret), K(item));
  }
  return ret;
}

int ObImportArg::add_remap_tablegroup(const ObRemapTablegroupItem &item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(remap_table_arg_.add_remap_tablegroup(item))) {
    LOG_WARN("failed to add remap tablegroup", K(ret), K(item));
  }
  return ret;
}

int ObImportArg::add_remap_tablespace(const ObRemapTablespaceItem &item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(remap_table_arg_.add_remap_tablespace(item))) {
    LOG_WARN("failed to add remap tablespace", K(ret), K(item));
  }
  return ret;
}

}
}
