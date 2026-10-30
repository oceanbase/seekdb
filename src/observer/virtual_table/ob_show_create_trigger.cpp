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

#include "observer/virtual_table/rowkey_filter_value.h"
#include "observer/virtual_table/ob_show_create_trigger.h"
#include "sql/printer/ob_schema_printer.h"
#include "sql/session/ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace observer
{

ObShowCreateTrigger::ObShowCreateTrigger() : ObVirtualTableScannerIterator()
{
}

ObShowCreateTrigger::~ObShowCreateTrigger()
{
}

void ObShowCreateTrigger::reset()
{
  ObVirtualTableScannerIterator::reset();
}

int ObShowCreateTrigger::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_guard_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "schema guard is NULL", K(ret), K(schema_guard_));
  } else if (!start_to_read_) {
    const ObTriggerInfo *tg_info = NULL;
    uint64_t tg_id = OB_INVALID_ID;
    if (OB_FAIL(calc_show_trigger_id(tg_id))) {
    } else if (OB_UNLIKELY(OB_INVALID_ID == tg_id)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_USER_ERROR(OB_ERR_UNEXPECTED, "this trigger is used for show clause, can't be selected");
    } else if (OB_FAIL(schema_guard_->get_trigger_info( tg_id, tg_info))) {
    } else if (OB_UNLIKELY(NULL == tg_info)) {
      ret = OB_ERR_TRIGGER_NOT_EXIST;
      SERVER_LOG(WARN, "fail to get trigger info", K(ret), K(tg_id));
    } else {
      if (OB_FAIL(fill_row_cells(tg_id, *tg_info))) {
      } else if (OB_FAIL(scanner_.add_row(cur_row_))) {
      } else {
        scanner_it_ = scanner_.begin();
        start_to_read_ = true;
      }
    }
  }
  if (OB_SUCCESS == ret && start_to_read_) {
    if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        SERVER_LOG(WARN, "fail to get next row", K(ret));
      }
    } else {
      row = &cur_row_;
    }
  }
  return ret;
}

int ObShowCreateTrigger::calc_show_trigger_id(uint64_t &tg_id)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCCESS == ret && OB_INVALID_ID == tg_id && i < key_ranges_.count(); ++i) {
    const ObRowkey &start_key = key_ranges_.at(i).start_key_;
    const ObRowkey &end_key = key_ranges_.at(i).end_key_;
    const DatumValue *start_key_obj_ptr = start_key.get_values();
    const DatumValue *end_key_obj_ptr = end_key.get_values();
    if (start_key.get_value_count() > 0 && start_key.get_value_count() == end_key.get_value_count()) {
      if (OB_UNLIKELY(NULL == start_key_obj_ptr || NULL == end_key_obj_ptr)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "key obj ptr is NULL", K(ret), K(start_key_obj_ptr), K(end_key_obj_ptr));
      } else if (key_filter_equal(start_key_obj_ptr[0], end_key_obj_ptr[0])
                 && ObIntType == start_key_obj_ptr[0].get_type()) {
        tg_id = start_key_obj_ptr[0].get_int();
      } else {/*do nothing*/}
    }
  }
  return ret;
}

int ObShowCreateTrigger::fill_row_cells(uint64_t tg_id, const ObTriggerInfo &tg_info)
{
  int ret = OB_SUCCESS;
  uint64_t cell_idx = 0;
  char *tg_def_buf = NULL;
  int64_t tg_def_buf_size = OB_MAX_VARCHAR_LENGTH;
  sql::ObExecEnv exec_env;
  if (OB_UNLIKELY(NULL == schema_guard_
                  || NULL == session_
                  || NULL == allocator_
                  || NULL == cur_row_.cells_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN,
               "data member isn't init",
               K(ret),
               K(schema_guard_),
               K(session_),
               K(allocator_),
               K(cur_row_.cells_));
  } else if (OB_UNLIKELY(cur_row_.count_ < output_column_ids_.count())) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN,
               "cells count is less than output column count",
               K(ret),
               K(cur_row_.count_),
               K(output_column_ids_.count()));
  } else if (OB_UNLIKELY(NULL == (tg_def_buf = 
                                  static_cast<char *>(allocator_->alloc(tg_def_buf_size))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SERVER_LOG(ERROR, "fail to alloc table_def_buf", K(ret));
  } else if (OB_FAIL(exec_env.init(tg_info.get_package_exec_env()))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch(col_id) {
        case TRIGGER_ID: {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cur_row_.cells_, cur_row_.count_, cell_idx, tg_id) : ret);
          break;
        }
        case SQL_MODE: {
          const int64_t sql_mode_value = exec_env.get_sql_mode();
          DatumValue int_value;
          int_value.meta_.set_int();
          int_value.datum_.set_string(reinterpret_cast<const char *>(&sql_mode_value), sizeof(sql_mode_value));
          if (OB_FAIL(ob_sql_mode_to_str(int_value,  cur_row_.cells_[cell_idx], allocator_))) {
          } else {
            cur_row_.cells_[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                          ObCharset::get_default_charset()));
          }
          break;
        }
        case CREATE_TRIGGER: {
          ObSchemaPrinter schema_printer(*schema_guard_);
          int64_t pos = 0;
          OZ (schema_printer.print_trigger_definition(tg_info, tg_def_buf, 
                                                      OB_MAX_VARCHAR_LENGTH, pos));
          OX ((OB_SUCC(ret) ? ret = DatumValueBuffer::set_lob_view(cur_row_.cells_, cur_row_.count_, cell_idx, ObLongTextType, ObString(static_cast<int32_t>(pos), tg_def_buf)) : ret));
          OX (cur_row_.cells_[cell_idx].set_collation_type(
                      ObCharset::get_default_collation(ObCharset::get_default_charset())));
          break;
        }
#define COLUMN_SET_WITH_TYPE(COL_NAME, TYPE, VALUE) \
  case (COL_NAME): {    \
    if (OB_SUCC(ret)) { ret = DatumValueBuffer::set_##TYPE(cur_row_.cells_, cur_row_.count_, cell_idx, VALUE); } \
    cur_row_.cells_[cell_idx].set_collation_type( \
                      ObCharset::get_default_collation(ObCharset::get_default_charset())); \
    break;\
  }
        COLUMN_SET_WITH_TYPE(TRIGGER_NAME, varchar_view, tg_info.get_trigger_name())
        COLUMN_SET_WITH_TYPE(CHARACTER_SET_CLIENT, varchar_view,
                             ObCharset::charset_name(exec_env.get_charset_client()))
        COLUMN_SET_WITH_TYPE(COLLATION_CONNECTION, varchar_view,
                             ObCharset::charset_name(exec_env.get_collation_connection()))
        COLUMN_SET_WITH_TYPE(COLLATION_DATABASE, varchar_view,
                             ObCharset::charset_name(exec_env.get_collation_database()))
#undef COLUMN_SET_WITH_TYPE
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(cell_idx),
                     K(i), K(output_column_ids_), K(col_id));
          break;
        }
      }
      if (OB_SUCC(ret)) {
        cell_idx++;
      }
    }
  }
  return ret;
}

} // observer
} // oceanbase
