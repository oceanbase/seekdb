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

#ifndef _OCEANBASE_SHARE_INNER_TABLE_OB_DUMP_INNER_TABLE_SCHEMA_H_
#define _OCEANBASE_SHARE_INNER_TABLE_OB_DUMP_INNER_TABLE_SCHEMA_H_ 1

#include "share/schema/ob_table_schema.h"
#include "share/inner_table/ob_load_inner_table_schema.h"
#include "src/share/ob_core_table_proxy.h"

// this code should only run in unit test, but it is important, so we put it here

namespace oceanbase
{
namespace share
{
class ObDMLSqlSplicer;
class ObDumpInnerTableSchemaUtils
{
public:
  static int upper(ObSqlString &str);
  static int replace(ObSqlString &str, const char a, const char b);
  static int lstrip(ObSqlString &str, const char c);
  static int rstrip(ObSqlString &str, const char c);
  static int strip(ObSqlString &str, const char c);
  static int table_name2tid(const ObString &table_name, ObSqlString &tid);
  static int table_name2tname(const ObString &table_name, ObSqlString &tname);
  static int table_name2schema_version(const schema::ObTableSchema &table, ObSqlString &schema_version);
};
class ObInnerTableSchemaDumper
{
public:
  ObInnerTableSchemaDumper(ObIArray<schema::ObTableSchema> &schemas, ObIAllocator &allocator) :
    schemas_(schemas), allocator_(allocator) {}
  int get_inner_table_schema_info(ObIArray<ObLoadInnerTableSchemaInfo> &infos);
private:
  int get_schema_pointers_(ObIArray<schema::ObTableSchema *> &schema_ptrs);
  int get_all_table_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs, ObLoadInnerTableSchemaInfo &info,
                          ObLoadInnerTableSchemaInfo &info_history);
  int get_all_column_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs, ObLoadInnerTableSchemaInfo &info,
                           ObLoadInnerTableSchemaInfo &info_history);
  int get_all_ddl_operation_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs, ObLoadInnerTableSchemaInfo &info);
  int get_all_core_table_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs, ObLoadInnerTableSchemaInfo &info);
  int get_table_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs,
      const ObString &table_name, const ObString &table_name_history,
      const uint64_t table_id, const uint64_t table_id_history,
      ObLoadInnerTableSchemaInfo &info, ObLoadInnerTableSchemaInfo &info_history);
  int get_column_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs,
      const ObString &table_name, const ObString &table_name_history,
      const uint64_t table_id, const uint64_t table_id_history,
      ObLoadInnerTableSchemaInfo &info, ObLoadInnerTableSchemaInfo &info_history);
private:
  ObIArray<schema::ObTableSchema> &schemas_;
  ObIAllocator &allocator_;
};
class ObLoadInnerTableSchemaInfoConstructor
{
public:
  ObLoadInnerTableSchemaInfoConstructor(const ObString &table_name, const uint64_t table_id,
      ObIAllocator &allocator)
    : table_id_(table_id), table_name_(table_name), rows_(), table_ids_(), header_(),
    allocator_(allocator) {}
  virtual ~ObLoadInnerTableSchemaInfoConstructor() {}
  virtual int add_lines(const uint64_t table_id, ObDMLSqlSplicer &splicer) = 0;
  int get_load_info(ObLoadInnerTableSchemaInfo &info);
  bool is_valid() const;

  uint64_t get_table_id() const { return table_id_; }
  ObString get_table_name() const { return table_name_; }
  ObString get_header() const { return header_; }
  const ObIArray<ObString> &get_rows() const { return rows_; }
  const ObIArray<uint64_t> &get_table_ids() const { return table_ids_; }
  ObIAllocator &get_allocator() { return allocator_; }
  TO_STRING_KV(K_(table_id), K_(table_name), K_(header), K(rows_.count()), K(table_ids_.count()),
      KP_(&allocator));
protected:
  int add_line(const ObString &line, const uint64_t table_id);
protected:
  uint64_t table_id_;
  ObString table_name_;
  ObArray<ObString> rows_;
  ObArray<uint64_t> table_ids_;
  ObString header_;
  ObIAllocator &allocator_;
};
class ObNotCoreTableLoadInfoConstructor : public ObLoadInnerTableSchemaInfoConstructor
{
public:
  ObNotCoreTableLoadInfoConstructor(const ObString &table_name, const uint64_t table_id, ObIAllocator &allocator) 
    : ObLoadInnerTableSchemaInfoConstructor(table_name, table_id, allocator) {}
  virtual int add_lines(const uint64_t table_id, ObDMLSqlSplicer &splicer) override;
  int add_header(ObDMLSqlSplicer &splicer);
};
class ObCoreTableLoadInfoConstructor : public ObLoadInnerTableSchemaInfoConstructor
{
public:
  ObCoreTableLoadInfoConstructor(const ObString core_table_name, ObIAllocator &allocator) :
    ObLoadInnerTableSchemaInfoConstructor(OB_ALL_CORE_TABLE_TNAME, OB_ALL_CORE_TABLE_TID, allocator),
    core_table_name_(core_table_name), store_cell_(allocator), row_id_(0) {}
  virtual int add_lines(const uint64_t table_id, ObDMLSqlSplicer &splicer) override;
private:
  class DumpCoreTableStoreCell : public ObCoreTableStoreCell
  {
  public:
    DumpCoreTableStoreCell(ObIAllocator &allocator) : allocator_(allocator) {}
    virtual int store_cell(const ObCoreTableCell &src, ObCoreTableCell &dest) override;
    int store_string(const common::ObString &src, common::ObString &dest);
  private:
    ObIAllocator &allocator_;
  };
private:
  ObString core_table_name_;
  DumpCoreTableStoreCell store_cell_;
  uint64_t row_id_;
};
class ObMergeLoadInfoConstructor : public ObLoadInnerTableSchemaInfoConstructor
{
public:
  ObMergeLoadInfoConstructor(const ObString &table_name, const uint64_t table_id, ObIAllocator &allocator) 
    : ObLoadInnerTableSchemaInfoConstructor(table_name, table_id, allocator) {}
  virtual int add_lines(const uint64_t table_id, ObDMLSqlSplicer &splicer) override;
  int add_constructor(ObLoadInnerTableSchemaInfoConstructor &constructor);
};

template <typename T, T (schema::ObSimpleTableSchemaV2::*func)() const>
class TableSchemaCmp
{
public:
  bool operator()(const schema::ObTableSchema *left, const schema::ObTableSchema *right) const 
  {
    bool ret = false;
    static_assert(func != nullptr, "func should not be null");
    if (OB_ISNULL(left)) {
      ret = false;
    } else if (OB_ISNULL(right)) {
      ret = true;
    } else {
      ret = ((left->*func)() < (right->*func)());
    }
    return ret;
  }
};

using TableSchemaCmpByTableId = TableSchemaCmp<uint64_t, &schema::ObTableSchema::get_table_id>;
using TableSchemaCmpBySchemaVersion = TableSchemaCmp<int64_t, &schema::ObTableSchema::get_schema_version>;

}
}

#endif // _OCEANBASE_SHARE_INNER_TABLE_OB_DUMP_INNER_TABLE_SCHEMA_H_
