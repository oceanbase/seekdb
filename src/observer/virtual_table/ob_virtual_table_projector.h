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

#ifndef OCEANBASE_COMMON_OB_ALL_VIRTUAL_TABLE_PROJECTOR_H_
#define OCEANBASE_COMMON_OB_ALL_VIRTUAL_TABLE_PROJECTOR_H_

#include "lib/container/ob_iarray.h"
#include "common/datum/datum_value_buffer.h"
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "share/schema/ob_schema_getter_guard.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
}
}
namespace common
{
class ObVirtualTableProjector : public common::ObVirtualTableScannerIterator
{
public:
  ObVirtualTableProjector() {}
  virtual ~ObVirtualTableProjector() {}
protected:
  struct Column {
    Column() : column_id_(common::OB_INVALID_ID), column_value_(), fixed_payload_{} {}
    Column(const Column &other) : Column() { *this = other; }
    Column &operator=(const Column &other)
    {
      if (this != &other) {
        column_id_ = other.column_id_;
        // Array relocation must rebind small native values to this Column's
        // reserved slot; variable payloads keep the producer's borrowed life.
        const int ret = common::DatumValueBuffer::assign_view(other.column_value_, &column_value_, 1, 0);
        OB_ASSERT(common::OB_SUCCESS == ret);
      }
      return *this;
    }

    TO_STRING_KV(K_(column_id), K_(column_value));

    uint64_t column_id_;
    common::DatumValue column_value_;
    alignas(8) char fixed_payload_[16];
  };
  static_assert(offsetof(Column, fixed_payload_) == offsetof(Column, column_value_) + sizeof(common::DatumValue),
                "Column reserves the native buffer scalar slot immediately after its descriptor");
  virtual int project_row(const common::ObIArray<Column> &columns, common::ObNewRow &row,
                          const bool full_columns = true);
  virtual int check_column_exist(const share::schema::ObTableSchema *table,
                                 const char* column_name,
                                 bool &exist) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObVirtualTableProjector);
};

#define ADD_TEXT_COLUMN(type, table_schema, column_name, column_value, columns) \
  do { \
    Column column; \
    const ObColumnSchemaV2 *column_schema = NULL; \
    if (OB_FAIL(ret)) { \
    } else if (NULL == table_schema || NULL == column_name) { \
      ret = OB_INVALID_ARGUMENT; \
      LOG_WARN("invalid argument", KP(table_schema), KP(column_name), K(ret)); \
    } else if (NULL == (column_schema = table_schema->get_column_schema(column_name))) { \
      LOG_WARN("column not exist, ignore this column", K(column_name), K(ret)); \
    } else { \
      ret = common::DatumValueBuffer::set_lob_view(&column.column_value_, 1, 0, type, column_value); \
      column.column_value_.set_collation_type(column_schema->get_collation_type()); \
      column.column_id_ = column_schema->get_column_id(); \
      if (OB_SUCC(ret) && OB_FAIL(columns.push_back(column))) { \
        LOG_WARN("push_back failed", K(ret)); \
      } \
    } \
  } while (false)

#define ADD_COLUMN(set_value_func, table_schema, column_name, column_value, columns) \
  do { \
    Column column; \
    const ObColumnSchemaV2 *column_schema = NULL; \
    if (OB_FAIL(ret)) { \
    } else if (NULL == table_schema || NULL == column_name) { \
      ret = OB_INVALID_ARGUMENT; \
      LOG_WARN("invalid argument", KP(table_schema), KP(column_name), K(ret)); \
    } else if (NULL == (column_schema = table_schema->get_column_schema(column_name))) { \
      LOG_WARN("column not exist, ignore this column", K(column_name), K(ret)); \
    } else { \
      ret = common::DatumValueBuffer::set_value_func(&column.column_value_, 1, 0, column_value); \
      column.column_value_.set_collation_type(column_schema->get_collation_type()); \
      column.column_id_ = column_schema->get_column_id(); \
      if (OB_SUCC(ret) && OB_FAIL(columns.push_back(column))) { \
        LOG_WARN("push_back failed", K(ret)); \
      } \
    } \
  } while (false)

#define ADD_NULL_COLUMN(table_schema, column_name, columns) \
  do { \
    Column column; \
    const ObColumnSchemaV2 *column_schema = NULL; \
    if (OB_FAIL(ret)) { \
    } else if (NULL == table_schema || NULL == column_name) { \
      ret = OB_INVALID_ARGUMENT; \
      LOG_WARN("invalid argument", KP(table_schema), KP(column_name), K(ret)); \
    } else if (NULL == (column_schema = table_schema->get_column_schema(column_name))) { \
      LOG_WARN("column not exist, ignore this column", K(column_name), K(ret)); \
    } else { \
      column.column_value_.set_null(); \
      column.column_id_ = column_schema->get_column_id(); \
      if (OB_SUCC(ret) && OB_FAIL(columns.push_back(column))) { \
        LOG_WARN("push_back failed", K(ret)); \
      } \
    } \
  } while (false)

class ObSimpleVirtualTableIterator: public ObVirtualTableProjector
{
public:
  ObSimpleVirtualTableIterator(uint64_t table_id);
  virtual ~ObSimpleVirtualTableIterator() {}

  virtual int inner_open() override;
  virtual int inner_get_next_row(common::ObNewRow *&row) override;

  // interface for subclass:
  virtual int get_next_full_row(const share::schema::ObTableSchema *table,
                            common::ObIArray<Column> &columns) = 0;
  virtual int init_all_data() { return common::OB_SUCCESS; }
private:
  // types and constants
  static const int64_t MAX_COLUMN_NUM = 32;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObSimpleVirtualTableIterator);
  // function members
  int get_table_schema( uint64_t table_id);
private:
  // data members
  uint64_t table_id_;
  common::ObSEArray<Column, MAX_COLUMN_NUM> columns_;
  share::schema::ObSchemaGetterGuard schema_guard_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  const share::schema::ObTableSchema *table_schema_;
};

}//end namespace common
}//end namespace oceanbase

#endif //OCEANBASE_COMMON_OB_ALL_VIRTUAL_TABLE_PROJECTOR_H_
