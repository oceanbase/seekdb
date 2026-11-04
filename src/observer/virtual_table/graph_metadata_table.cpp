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

#define USING_LOG_PREFIX SERVER
#include "observer/virtual_table/graph_metadata_table.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_table_schema.h"
#include "share/ob_lob_access_utils.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace share::schema;

int GraphMetadataTable::inner_open()
{
  graphs_.reuse();
  graph_pos_ = element_pos_ = detail_pos_ = endpoint_pos_ = 0;
  access_checked_ = false;
  row_allocator_.reuse();
  int ret = OB_SUCCESS;
  if (schema_guard_ == nullptr || session_ == nullptr || table_schema_ == nullptr) {
    ret = OB_NOT_INIT;
  } else {
    ret = schema_guard_->get_graph_schemas(graphs_);
  }
  return ret;
}

void GraphMetadataTable::reset()
{
  graphs_.reset();
  graph_pos_ = element_pos_ = detail_pos_ = endpoint_pos_ = 0;
  access_checked_ = false;
  row_allocator_.reset();
  ObVirtualTableIterator::reset();
}

void GraphMetadataTable::next_graph()
{
  ++graph_pos_;
  element_pos_ = detail_pos_ = endpoint_pos_ = 0;
  access_checked_ = false;
}

int GraphMetadataTable::put(const char *name, const ObObj &value)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
    const ObColumnSchemaV2 *column = table_schema_->get_column_schema(output_column_ids_.at(i));
    if (column == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else if (column->get_column_name_str() == name) {
      cur_row_.cells_[i] = value;
    }
  }
  return ret;
}

int GraphMetadataTable::put_string(const char *name, const ObString &value)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
    const ObColumnSchemaV2 *column = table_schema_->get_column_schema(output_column_ids_.at(i));
    if (column == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else if (column->get_column_name_str() == name) {
      ObString copy;
      if (OB_FAIL(ob_write_string(row_allocator_, value, copy))) {
      } else {
        ObObj &cell = cur_row_.cells_[i];
        cell.set_string(column->get_data_type(), copy);
        cell.set_collation_type(column->get_collation_type());
        if (ob_is_text_tc(column->get_data_type())) {
          ret = ObTextStringResult::ob_convert_obj_temporay_lob(cell, row_allocator_);
        }
      }
    }
  }
  return ret;
}

int GraphMetadataTable::put_number(const char *name, uint64_t value)
{
  ObObj cell;
  cell.set_uint64(value);
  return put(name, cell);
}

int GraphMetadataTable::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  row_allocator_.reuse();
  if (schema_guard_ == nullptr || session_ == nullptr || table_schema_ == nullptr
      || cur_row_.cells_ == nullptr || output_column_ids_.count() > cur_row_.count_) {
    ret = OB_NOT_INIT;
  }
  while (OB_SUCC(ret) && row == nullptr && graph_pos_ < graphs_.count()) {
    const GraphSchema *graph = graphs_.at(graph_pos_);
    const ObDatabaseSchema *database = nullptr;
    if (OB_FAIL(THIS_WORKER.check_status())) {
    } else if (graph == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(schema_guard_->get_database_schema(graph->get_database_id(), database))) {
    } else if (database == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!access_checked_) {
      // Match SHOW CREATE PROPERTY GRAPH's database-access policy. Never cache
      // visibility across opens/executions, including prepared statements.
      ObSessionPrivInfo privileges;
      ObPrivSet db_priv = OB_PRIV_SET_EMPTY;
      bool matches = false;
      if (OB_FAIL(matches_range(database->get_database_name_str(), graph->get_name(), matches))) {
      } else if (!matches) {
        next_graph();
        continue;
      } else if (OB_FAIL(session_->get_session_priv_info(privileges))) {
      } else if (OB_FAIL(schema_guard_->check_db_access(privileges,
          session_->get_enable_role_array(), database->get_database_name_str(), db_priv, false))) {
        if (ret == OB_ERR_NO_DB_PRIVILEGE) {
          ret = OB_SUCCESS;
          next_graph();
          continue;
        }
      } else {
        access_checked_ = true;
      }
    }
    if (OB_SUCC(ret)) {
      // Reset metadata as well as values: a previous LONGTEXT cell may carry
      // the temporary-LOB header flag, which must not leak into a new string.
      for (int64_t i = 0; i < cur_row_.count_; ++i) { cur_row_.cells_[i].reset(); }
      if (OB_FAIL(fill_row(*graph, *database))) {
        if (ret == OB_ITER_END) {
          ret = OB_SUCCESS;
          next_graph();
        }
      } else {
        row = &cur_row_;
      }
    }
  }
  if (OB_SUCC(ret) && row == nullptr) { ret = OB_ITER_END; }
  return ret;
}

int GraphMetadataTable::matches_range(const ObString &database, const ObString &graph, bool &matches)
{
  int ret = OB_SUCCESS;
  // Predicates extracted as scan ranges need not remain as SQL filters.
  // Honor every range, including IN lists and exclusive bounds.
  ObObj cells[2];
  cells[0].set_varchar(database);
  cells[1].set_varchar(graph);
  for (int64_t i = 0; OB_SUCC(ret) && i < 2; ++i) {
    const ObColumnSchemaV2 *column = table_schema_->get_column_schema(OB_APP_MIN_COLUMN_ID + i);
    if (column == nullptr) { ret = OB_ERR_UNEXPECTED; }
    else { cells[i].set_collation_type(column->get_collation_type()); }
  }
  const ObRowkey key(cells, 2);
  matches = key_ranges_.empty();
  for (int64_t i = 0; OB_SUCC(ret) && !matches && i < key_ranges_.count(); ++i) {
    const ObNewRange &range = key_ranges_.at(i);
    const int lower = key.compare(range.start_key_);
    const int upper = key.compare(range.end_key_);
    matches = (lower > 0 || (lower == 0 && range.border_flag_.inclusive_start()))
        && (upper < 0 || (upper == 0 && range.border_flag_.inclusive_end()));
  }
  return ret;
}

int GraphMetadataTable::fill_row(const GraphSchema &graph, const ObDatabaseSchema &database)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(put_string("GRAPH_SCHEMA", database.get_database_name_str()))) {
  } else if (OB_FAIL(put_string("GRAPH_NAME", graph.get_name()))) {
  } else if (kind_ == Kind::GRAPHS) {
    next_graph();
  } else {
    bool emitted = false;
    while (OB_SUCC(ret) && !emitted && element_pos_ < graph.get_elements().count()) {
      const GraphElement &element = graph.get_elements().at(element_pos_);
      const ObTableSchema *table = nullptr;
      if (OB_FAIL(THIS_WORKER.check_status())) {
      } else if (OB_FAIL(schema_guard_->get_table_schema(element.table_id_, table))) {
      } else if (table == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(put_string("ELEMENT_NAME", table->get_table_name_str()))) {
      } else if (OB_FAIL(put_string("LABEL_NAME", element.label_))) {
      } else if (kind_ == Kind::ELEMENTS) {
        const ObDatabaseSchema *source_db = nullptr;
        if (OB_FAIL(schema_guard_->get_database_schema(table->get_database_id(), source_db))) {
        } else if (source_db == nullptr) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(put_string("ELEMENT_KIND", ObString::make_string(element.is_vertex() ? "VERTEX" : "EDGE")))) {
        } else if (OB_FAIL(put_string("OBJECT_SCHEMA", source_db->get_database_name_str()))) {
        } else if (OB_FAIL(put_string("OBJECT_NAME", table->get_table_name_str()))) {
        } else if (OB_FAIL(put_string("OBJECT_TYPE", ObString::make_string(table->is_view_table() ? "VIEW" : "BASE TABLE")))) {
        } else {
          emitted = true;
          ++element_pos_;
        }
      } else if (kind_ == Kind::LABELS || kind_ == Kind::ELEMENT_LABELS) {
        // Current schemas require distinct labels and one label per element.
        // The public LABELS grain is one row per graph label, not per element.
        emitted = true;
        ++element_pos_;
      } else if (kind_ == Kind::KEYS) {
        const ObColumnSchemaV2 *column = table->get_column_schema(element.key_columns_[detail_pos_]);
        if (column == nullptr) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(put_number("ORDINAL_POSITION", detail_pos_ + 1))) {
        } else if (OB_FAIL(put_string("COLUMN_NAME", column->get_column_name_str()))) {
        } else {
          emitted = true;
          if (++detail_pos_ == element.key_count_) {
            detail_pos_ = 0;
            ++element_pos_;
          }
        }
      } else if (kind_ == Kind::EDGE_RELATIONSHIPS) {
        if (element.is_vertex()) {
          ++element_pos_;
        } else {
          const bool source = endpoint_pos_ == 0;
          const GraphElement *vertex = graph.get_element(source ? element.source_id_ : element.destination_id_);
          const ObTableSchema *vertex_table = nullptr;
          const uint64_t edge_column_id = source ? element.source_columns_[detail_pos_] : element.destination_columns_[detail_pos_];
          const ObColumnSchemaV2 *edge_column = table->get_column_schema(edge_column_id);
          const ObColumnSchemaV2 *vertex_column = nullptr;
          if (vertex == nullptr || edge_column == nullptr) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(schema_guard_->get_table_schema(vertex->table_id_, vertex_table))) {
          } else if (vertex_table == nullptr || (vertex_column = vertex_table->get_column_schema(vertex->key_columns_[detail_pos_])) == nullptr) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(put_string("EDGE_ELEMENT_NAME", table->get_table_name_str()))) {
          } else if (OB_FAIL(put_string("ENDPOINT_TYPE", ObString::make_string(source ? "SOURCE" : "DESTINATION")))) {
          } else if (OB_FAIL(put_string("VERTEX_ELEMENT_NAME", vertex_table->get_table_name_str()))) {
          } else if (OB_FAIL(put_number("ORDINAL_POSITION", detail_pos_ + 1))) {
          } else if (OB_FAIL(put_string("EDGE_COLUMN_NAME", edge_column->get_column_name_str()))) {
          } else if (OB_FAIL(put_string("VERTEX_COLUMN_NAME", vertex_column->get_column_name_str()))) {
          } else {
            emitted = true;
            if (++detail_pos_ == (source ? element.source_key_count_ : element.destination_key_count_)) {
              detail_pos_ = 0;
              if (++endpoint_pos_ == 2) {
                endpoint_pos_ = 0;
                ++element_pos_;
              }
            }
          }
        }
      } else {
        // Definitions belong to (element, property); label exposure is a
        // separate relation even though today's mappings are one-to-one.
        const GraphProperty *property = nullptr;
        int64_t ordinal = 0;
        for (int64_t i = 0; property == nullptr && i < graph.get_properties().count(); ++i) {
          if (graph.get_properties().at(i).element_id_ == element.id_ && ordinal++ == detail_pos_) {
            property = &graph.get_properties().at(i);
          }
        }
        if (property == nullptr) {
          detail_pos_ = 0;
          ++element_pos_;
        } else {
          const ObColumnSchemaV2 *column = table->get_column_schema(property->column_id_);
          if (column == nullptr) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(put_string("PROPERTY_NAME", property->name_))) {
          } else if (OB_FAIL(put_number("ORDINAL_POSITION", detail_pos_ + 1))) {
          } else if (OB_FAIL(fill_type(*column))) {
          } else if (kind_ == Kind::PROPERTY_DEFINITIONS) {
            ObSqlString expression;
            const ObString &name = column->get_column_name_str();
            if (OB_FAIL(expression.append("`"))) {
            }
            for (int64_t i = 0; OB_SUCC(ret) && i < name.length(); ++i) {
              if (name.ptr()[i] == '`') { ret = expression.append("`"); }
              if (OB_SUCC(ret)) { ret = expression.append(name.ptr() + i, 1); }
            }
            if (OB_SUCC(ret) && OB_FAIL(expression.append("`"))) {
            }
            if (OB_SUCC(ret)) {
              if (OB_FAIL(put_string("COLUMN_NAME", name))) {
              } else if (OB_FAIL(put_string("VALUE_EXPRESSION", expression.string()))) {
              } else if (OB_FAIL(put_string("IS_NULLABLE", ObString::make_string(column->is_nullable() ? "YES" : "NO")))) {
              }
            }
          }
          if (OB_SUCC(ret)) {
            emitted = true;
            ++detail_pos_;
          }
        }
      }
    }
    if (OB_SUCC(ret) && !emitted) { ret = OB_ITER_END; }
  }
  return ret;
}

int GraphMetadataTable::fill_type(const ObColumnSchemaV2 &column)
{
  int ret = OB_SUCCESS;
  // Use the same SQL type printers and size conventions as COLUMNS, including
  // extended types, enum/set, unsigned and zerofill. No graph-only type names.
  const int64_t capacity = OB_MAX_EXTENDED_TYPE_INFO_LENGTH;
  char *buffer = static_cast<char *>(row_allocator_.alloc(capacity));
  int64_t pos = 0;
  const ObObjType type = column.get_data_type();
  if (buffer == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ob_sql_type_str(buffer, capacity, type, column.get_collation_type(),
      column.get_extended_type_info(), column.get_geo_type(), column.is_string_lob()))) {
  } else if (OB_FAIL(put_string("DATA_TYPE", ObString::make_string(buffer)))) {
  } else if (OB_FAIL(ob_sql_type_str(column.get_meta_type(), column.get_accuracy(),
      column.get_extended_type_info(), LS_BYTE, buffer, capacity, pos,
      column.is_extend() ? column.get_sub_data_type() : static_cast<uint64_t>(column.get_geo_type()),
      column.is_string_lob()))) {
  } else if (column.is_zero_fill() && OB_FAIL(databuff_printf(buffer, capacity, pos, " zerofill"))) {
  } else if (OB_FAIL(put_string("COLUMN_TYPE", ObString(static_cast<int32_t>(pos), buffer)))) {
  }
  if (OB_SUCC(ret) && (ob_is_string_type(type) || ob_is_json(type) || ob_is_geometry(type))) {
    ret = put_number("CHARACTER_MAXIMUM_LENGTH", column.get_data_length());
  }
  if (OB_SUCC(ret)) {
    if (ob_is_string_tc(type)) {
      int64_t max_bytes = 0;
      if (OB_FAIL(ObCharset::get_mbmaxlen_by_coll(column.get_collation_type(), max_bytes))) {
      } else {
        ret = put_number("CHARACTER_OCTET_LENGTH", max_bytes * column.get_data_length());
      }
    } else if (ob_is_text_tc(type) || ob_is_json(type) || ob_is_geometry(type)) {
      ret = put_number("CHARACTER_OCTET_LENGTH", column.get_data_length());
    }
  }
  ObPrecision precision = column.get_data_precision();
  if (column.get_data_scale() < 0) {
    if (ob_is_float_type(type)) { precision = MAX_FLOAT_STR_LENGTH; }
    else if (ob_is_double_type(type)) { precision = MAX_DOUBLE_STR_LENGTH; }
  }
  if (OB_SUCC(ret) && ob_is_numeric_type(type) && precision >= 0) {
    ret = put_number("NUMERIC_PRECISION", precision);
  }
  if (OB_SUCC(ret) && ob_is_numeric_type(type) && column.get_data_scale() >= 0) {
    ret = put_number("NUMERIC_SCALE", column.get_data_scale());
  }
  if (OB_SUCC(ret) && (ob_is_datetime_tc(type) || ob_is_time_tc(type) || ob_is_mysql_datetime_tc(type))) {
    ret = put_number("DATETIME_PRECISION", column.get_data_scale());
  }
  if (OB_SUCC(ret) && (ob_is_varchar_char_type(type, column.get_collation_type())
      || ob_is_enum_or_set_type(type) || ob_is_text(type, column.get_collation_type()))) {
    if (OB_FAIL(put_string("CHARACTER_SET_NAME", ObString::make_string(ObCharset::charset_name(column.get_collation_type()))))) {
    } else if (OB_FAIL(put_string("COLLATION_NAME", ObString::make_string(ObCharset::collation_name(column.get_collation_type()))))) {
    }
  }
  return ret;
}
} // namespace observer
} // namespace oceanbase
