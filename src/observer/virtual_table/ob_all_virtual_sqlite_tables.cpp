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

#include "observer/virtual_table/ob_all_virtual_sqlite_tables.h"
#include "share/ob_server_struct.h"
#include "share/storage/ob_sqlite_connection.h"
#include "lib/oblog/ob_log.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_print_utils.h"
#include <sqlite/sqlite3.h>

namespace oceanbase
{
using namespace common;
using namespace share;

namespace observer
{

ObAllVirtualSysParameter::ObAllVirtualSysParameter()
  : is_inited_(false),
    guard_(),
    stmt_(nullptr),
    row_idx_(0),
    name_(),
    gmt_create_(0),
    gmt_modified_(0),
    data_type_(),
    value_(),
    value_strict_(),
    info_(),
    need_reboot_(0),
    section_(),
    visible_level_(),
    config_version_(0),
    scope_(),
    source_(),
    edit_level_()
{
}

ObAllVirtualSysParameter::~ObAllVirtualSysParameter()
{
  reset();
}

void ObAllVirtualSysParameter::reset()
{
  if (stmt_) {
    guard_->finalize_query(stmt_);
    stmt_ = nullptr;
  }
  is_inited_ = false;
  row_idx_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualSysParameter::inner_open()
{
  int ret = OB_SUCCESS;

  ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
  if (!guard) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to acquire connection", K(ret));
  } else {
    guard_ = std::move(guard);
    const char *select_sql =
      "SELECT name, gmt_create, gmt_modified, data_type, value, value_strict, info, need_reboot, section, visible_level, config_version, scope, source, edit_level "
      "FROM __all_sys_parameter;";

    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {
      SERVER_LOG(WARN, "failed to prepare query", K(ret));
    } else {
      is_inited_ = true;
      row_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualSysParameter::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not initialized", K(ret));
  } else {
    ret = get_next_row_from_sqlite();
    if (OB_ITER_END == ret) {
      // End of result set
    } else if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));
    } else if (OB_FAIL(fill_cells())) {
      SERVER_LOG(WARN, "failed to fill cells", K(ret));
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

int ObAllVirtualSysParameter::get_next_row_from_sqlite()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));
  } else {
    ObSQLiteRowReader reader;
    ret = guard_->step_query(stmt_, reader);
    if (OB_SUCC(ret)) {
      int name_len = 0;
      const char *name_str = reader.get_text(&name_len);
      if (OB_ISNULL(name_str)) {
        name_.reset();
      } else {
        name_.assign_ptr(name_str, name_len);
      }
      gmt_create_ = reader.get_int64();
      gmt_modified_ = reader.get_int64();
      int data_type_len = 0;
      const char *data_type_str = reader.get_text(&data_type_len);
      if (OB_ISNULL(data_type_str)) {
        data_type_.reset();
      } else {
        data_type_.assign_ptr(data_type_str, data_type_len);
      }
      int value_len = 0;
      const char *value_str = reader.get_text(&value_len);
      if (OB_ISNULL(value_str)) {
        value_.reset();
      } else {
        value_.assign_ptr(value_str, value_len);
      }
      int value_strict_len = 0;
      const char *value_strict_str = reader.get_text(&value_strict_len);
      if (OB_ISNULL(value_strict_str)) {
        value_strict_.reset();
      } else {
        value_strict_.assign_ptr(value_strict_str, value_strict_len);
      }
      int info_len = 0;
      const char *info_str = reader.get_text(&info_len);
      if (OB_ISNULL(info_str)) {
        info_.reset();
      } else {
        info_.assign_ptr(info_str, info_len);
      }
      need_reboot_ = reader.get_int64();
      int section_len = 0;
      const char *section_str = reader.get_text(&section_len);
      if (OB_ISNULL(section_str)) {
        section_.reset();
      } else {
        section_.assign_ptr(section_str, section_len);
      }
      int visible_level_len = 0;
      const char *visible_level_str = reader.get_text(&visible_level_len);
      if (OB_ISNULL(visible_level_str)) {
        visible_level_.reset();
      } else {
        visible_level_.assign_ptr(visible_level_str, visible_level_len);
      }
      config_version_ = reader.get_int64();
      int scope_len = 0;
      const char *scope_str = reader.get_text(&scope_len);
      if (OB_ISNULL(scope_str)) {
        scope_.reset();
      } else {
        scope_.assign_ptr(scope_str, scope_len);
      }
      int source_len = 0;
      const char *source_str = reader.get_text(&source_len);
      if (OB_ISNULL(source_str)) {
        source_.reset();
      } else {
        source_.assign_ptr(source_str, source_len);
      }
      int edit_level_len = 0;
      const char *edit_level_str = reader.get_text(&edit_level_len);
      if (OB_ISNULL(edit_level_str)) {
        edit_level_.reset();
      } else {
        edit_level_.assign_ptr(edit_level_str, edit_level_len);
      }
    } else if (OB_ITER_END == ret) {
      // End of result set
    } else {
      SERVER_LOG(WARN, "failed to step query", K(ret));
    }
  }

  return ret;
}

int ObAllVirtualSysParameter::fill_cells()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0: {
          // name
          cells[i].set_varchar(name_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          // gmt_create
          cells[i].set_int(gmt_create_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: {
          // gmt_modified
          cells[i].set_int(gmt_modified_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: {
          // data_type
          cells[i].set_varchar(data_type_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 4: {
          // value
          cells[i].set_varchar(value_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 5: {
          // value_strict
          cells[i].set_varchar(value_strict_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 6: {
          // info
          cells[i].set_varchar(info_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 7: {
          // need_reboot
          cells[i].set_int(need_reboot_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 8: {
          // section
          cells[i].set_varchar(section_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 9: {
          // visible_level
          cells[i].set_varchar(visible_level_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 10: {
          // config_version
          cells[i].set_uint64(config_version_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 11: {
          // scope
          cells[i].set_varchar(scope_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 12: {
          // source
          cells[i].set_varchar(source_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 13: {
          // edit_level
          cells[i].set_varchar(edit_level_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
          break;
        }
      }
    }
  }

  return ret;
}

ObAllVirtualDeadlockEventHistory::ObAllVirtualDeadlockEventHistory()
  : is_inited_(false),
    guard_(),
    stmt_(nullptr),
    row_idx_(0),
    event_id_(0),
    detector_id_(0),
    report_time_(0),
    cycle_idx_(0),
    cycle_size_(0),
    role_(),
    priority_level_(),
    priority_(0),
    create_time_(0),
    start_delay_(0),
    module_(),
    visitor_(),
    object_(),
    extra_name1_(),
    extra_value1_(),
    extra_name2_(),
    extra_value2_(),
    extra_name3_(),
    extra_value3_()
{
}

ObAllVirtualDeadlockEventHistory::~ObAllVirtualDeadlockEventHistory()
{
  reset();
}

void ObAllVirtualDeadlockEventHistory::reset()
{
  if (stmt_) {
    guard_->finalize_query(stmt_);
    stmt_ = nullptr;
  }
  is_inited_ = false;
  row_idx_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualDeadlockEventHistory::inner_open()
{
  int ret = OB_SUCCESS;

  ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
  if (!guard) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to acquire connection", K(ret));
  } else {
    guard_ = std::move(guard);
    const char *select_sql =
      "SELECT event_id, detector_id, report_time, cycle_idx, cycle_size, role, priority_level, priority, create_time, start_delay, module, visitor, object, extra_name1, extra_value1, extra_name2, extra_value2, extra_name3, extra_value3 "
      "FROM __all_deadlock_event_history;";

    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {
      SERVER_LOG(WARN, "failed to prepare query", K(ret));
    } else {
      is_inited_ = true;
      row_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualDeadlockEventHistory::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not initialized", K(ret));
  } else {
    ret = get_next_row_from_sqlite();
    if (OB_ITER_END == ret) {
      // End of result set
    } else if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));
    } else if (OB_FAIL(fill_cells())) {
      SERVER_LOG(WARN, "failed to fill cells", K(ret));
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

int ObAllVirtualDeadlockEventHistory::get_next_row_from_sqlite()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));
  } else {
    ObSQLiteRowReader reader;
    ret = guard_->step_query(stmt_, reader);
    if (OB_SUCC(ret)) {
      event_id_ = reader.get_int64();
      detector_id_ = reader.get_int64();
      report_time_ = reader.get_int64();
      cycle_idx_ = reader.get_int64();
      cycle_size_ = reader.get_int64();
      int role_len = 0;
      const char *role_str = reader.get_text(&role_len);
      if (OB_ISNULL(role_str)) {
        role_.reset();
      } else {
        role_.assign_ptr(role_str, role_len);
      }
      int priority_level_len = 0;
      const char *priority_level_str = reader.get_text(&priority_level_len);
      if (OB_ISNULL(priority_level_str)) {
        priority_level_.reset();
      } else {
        priority_level_.assign_ptr(priority_level_str, priority_level_len);
      }
      priority_ = reader.get_int64();
      create_time_ = reader.get_int64();
      start_delay_ = reader.get_int64();
      int module_len = 0;
      const char *module_str = reader.get_text(&module_len);
      if (OB_ISNULL(module_str)) {
        module_.reset();
      } else {
        module_.assign_ptr(module_str, module_len);
      }
      int visitor_len = 0;
      const char *visitor_str = reader.get_text(&visitor_len);
      if (OB_ISNULL(visitor_str)) {
        visitor_.reset();
      } else {
        visitor_.assign_ptr(visitor_str, visitor_len);
      }
      int object_len = 0;
      const char *object_str = reader.get_text(&object_len);
      if (OB_ISNULL(object_str)) {
        object_.reset();
      } else {
        object_.assign_ptr(object_str, object_len);
      }
      int extra_name1_len = 0;
      const char *extra_name1_str = reader.get_text(&extra_name1_len);
      if (OB_ISNULL(extra_name1_str)) {
        extra_name1_.reset();
      } else {
        extra_name1_.assign_ptr(extra_name1_str, extra_name1_len);
      }
      int extra_value1_len = 0;
      const char *extra_value1_str = reader.get_text(&extra_value1_len);
      if (OB_ISNULL(extra_value1_str)) {
        extra_value1_.reset();
      } else {
        extra_value1_.assign_ptr(extra_value1_str, extra_value1_len);
      }
      int extra_name2_len = 0;
      const char *extra_name2_str = reader.get_text(&extra_name2_len);
      if (OB_ISNULL(extra_name2_str)) {
        extra_name2_.reset();
      } else {
        extra_name2_.assign_ptr(extra_name2_str, extra_name2_len);
      }
      int extra_value2_len = 0;
      const char *extra_value2_str = reader.get_text(&extra_value2_len);
      if (OB_ISNULL(extra_value2_str)) {
        extra_value2_.reset();
      } else {
        extra_value2_.assign_ptr(extra_value2_str, extra_value2_len);
      }
      int extra_name3_len = 0;
      const char *extra_name3_str = reader.get_text(&extra_name3_len);
      if (OB_ISNULL(extra_name3_str)) {
        extra_name3_.reset();
      } else {
        extra_name3_.assign_ptr(extra_name3_str, extra_name3_len);
      }
      int extra_value3_len = 0;
      const char *extra_value3_str = reader.get_text(&extra_value3_len);
      if (OB_ISNULL(extra_value3_str)) {
        extra_value3_.reset();
      } else {
        extra_value3_.assign_ptr(extra_value3_str, extra_value3_len);
      }
    } else if (OB_ITER_END == ret) {
      // End of result set
    } else {
      SERVER_LOG(WARN, "failed to step query", K(ret));
    }
  }

  return ret;
}

int ObAllVirtualDeadlockEventHistory::fill_cells()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0: {
          // event_id
          cells[i].set_int(event_id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          // detector_id
          cells[i].set_int(detector_id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: {
          // report_time
          cells[i].set_int(report_time_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: {
          // cycle_idx
          cells[i].set_int(cycle_idx_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 4: {
          // cycle_size
          cells[i].set_int(cycle_size_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 5: {
          // role
          cells[i].set_varchar(role_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 6: {
          // priority_level
          cells[i].set_varchar(priority_level_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 7: {
          // priority
          cells[i].set_int(priority_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 8: {
          // create_time
          cells[i].set_int(create_time_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 9: {
          // start_delay
          cells[i].set_int(start_delay_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 10: {
          // module
          cells[i].set_varchar(module_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 11: {
          // visitor
          cells[i].set_varchar(visitor_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 12: {
          // object
          cells[i].set_varchar(object_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 13: {
          // extra_name1
          cells[i].set_varchar(extra_name1_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 14: {
          // extra_value1
          cells[i].set_varchar(extra_value1_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 15: {
          // extra_name2
          cells[i].set_varchar(extra_name2_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 16: {
          // extra_value2
          cells[i].set_varchar(extra_value2_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 17: {
          // extra_name3
          cells[i].set_varchar(extra_name3_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 18: {
          // extra_value3
          cells[i].set_varchar(extra_value3_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
          break;
        }
      }
    }
  }

  return ret;
}

ObAllVirtualTabletMetaTable::ObAllVirtualTabletMetaTable()
  : is_inited_(false),
    guard_(),
    stmt_(nullptr),
    row_idx_(0),
    tablet_id_(0),
    gmt_create_(0),
    gmt_modified_(0),
    compaction_scn_(0),
    data_size_(0),
    required_size_(0),
    report_scn_(0),
    status_(0)
{
}

ObAllVirtualTabletMetaTable::~ObAllVirtualTabletMetaTable()
{
  reset();
}

void ObAllVirtualTabletMetaTable::reset()
{
  if (stmt_) {
    guard_->finalize_query(stmt_);
    stmt_ = nullptr;
  }
  is_inited_ = false;
  row_idx_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualTabletMetaTable::inner_open()
{
  int ret = OB_SUCCESS;

  ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
  if (!guard) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to acquire connection", K(ret));
  } else {
    guard_ = std::move(guard);
    const char *select_sql =
      "SELECT tablet_id, gmt_create, gmt_modified, compaction_scn, data_size, required_size, report_scn, status "
      "FROM __all_tablet_meta_table;";

    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {
      SERVER_LOG(WARN, "failed to prepare query", K(ret));
    } else {
      is_inited_ = true;
      row_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualTabletMetaTable::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not initialized", K(ret));
  } else {
    ret = get_next_row_from_sqlite();
    if (OB_ITER_END == ret) {
      // End of result set
    } else if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));
    } else if (OB_FAIL(fill_cells())) {
      SERVER_LOG(WARN, "failed to fill cells", K(ret));
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

int ObAllVirtualTabletMetaTable::get_next_row_from_sqlite()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));
  } else {
    ObSQLiteRowReader reader;
    ret = guard_->step_query(stmt_, reader);
    if (OB_SUCC(ret)) {
      tablet_id_ = reader.get_int64();
      gmt_create_ = reader.get_int64();
      gmt_modified_ = reader.get_int64();
      compaction_scn_ = reader.get_int64();
      data_size_ = reader.get_int64();
      required_size_ = reader.get_int64();
      report_scn_ = reader.get_int64();
      status_ = reader.get_int64();
    } else if (OB_ITER_END == ret) {
      // End of result set
    } else {
      SERVER_LOG(WARN, "failed to step query", K(ret));
    }
  }

  return ret;
}

int ObAllVirtualTabletMetaTable::fill_cells()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0: {
          // tablet_id
          cells[i].set_int(tablet_id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          // gmt_create
          cells[i].set_int(gmt_create_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: {
          // gmt_modified
          cells[i].set_int(gmt_modified_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: {
          // compaction_scn
          cells[i].set_uint64(compaction_scn_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 4: {
          // data_size
          cells[i].set_int(data_size_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 5: {
          // required_size
          cells[i].set_int(required_size_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 6: {
          // report_scn
          cells[i].set_uint64(report_scn_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 7: {
          // status
          cells[i].set_int(status_);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
          break;
        }
      }
    }
  }

  return ret;
}

ObAllVirtualTabletReplicaChecksum::ObAllVirtualTabletReplicaChecksum()
  : is_inited_(false),
    guard_(),
    stmt_(nullptr),
    row_idx_(0),
    tablet_id_(0),
    compaction_scn_(0),
    row_count_(0),
    data_checksum_(0),
    column_checksums_(),
    b_column_checksums_(),
    data_checksum_type_(0),
    co_base_snapshot_version_(0)
{
}

ObAllVirtualTabletReplicaChecksum::~ObAllVirtualTabletReplicaChecksum()
{
  reset();
}

void ObAllVirtualTabletReplicaChecksum::reset()
{
  if (stmt_) {
    guard_->finalize_query(stmt_);
    stmt_ = nullptr;
  }
  is_inited_ = false;
  row_idx_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualTabletReplicaChecksum::inner_open()
{
  int ret = OB_SUCCESS;

  ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
  if (!guard) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to acquire connection", K(ret));
  } else {
    guard_ = std::move(guard);
    const char *select_sql =
      "SELECT tablet_id, compaction_scn, row_count, data_checksum, column_checksums, b_column_checksums, data_checksum_type, co_base_snapshot_version "
      "FROM __all_tablet_replica_checksum;";

    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {
      SERVER_LOG(WARN, "failed to prepare query", K(ret));
    } else {
      is_inited_ = true;
      row_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualTabletReplicaChecksum::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not initialized", K(ret));
  } else {
    ret = get_next_row_from_sqlite();
    if (OB_ITER_END == ret) {
      // End of result set
    } else if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));
    } else if (OB_FAIL(fill_cells())) {
      SERVER_LOG(WARN, "failed to fill cells", K(ret));
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

int ObAllVirtualTabletReplicaChecksum::get_next_row_from_sqlite()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));
  } else {
    ObSQLiteRowReader reader;
    ret = guard_->step_query(stmt_, reader);
    if (OB_SUCC(ret)) {
      tablet_id_ = reader.get_int64();
      compaction_scn_ = reader.get_int64();
      row_count_ = reader.get_int64();
      data_checksum_ = reader.get_int64();
      int column_checksums_len = 0;
      const char *column_checksums_str = reader.get_text(&column_checksums_len);
      if (OB_ISNULL(column_checksums_str)) {
        column_checksums_.reset();
      } else {
        column_checksums_.assign_ptr(column_checksums_str, column_checksums_len);
      }
      int b_column_checksums_len = 0;
      const void *b_column_checksums_ptr = reader.get_blob(&b_column_checksums_len);
      if (OB_ISNULL(b_column_checksums_ptr)) {
        b_column_checksums_.reset();
      } else {
        b_column_checksums_.assign_ptr(static_cast<const char *>(b_column_checksums_ptr), b_column_checksums_len);
      }
      data_checksum_type_ = reader.get_int64();
      co_base_snapshot_version_ = reader.get_int64();
    } else if (OB_ITER_END == ret) {
      // End of result set
    } else {
      SERVER_LOG(WARN, "failed to step query", K(ret));
    }
  }

  return ret;
}

int ObAllVirtualTabletReplicaChecksum::fill_cells()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0: {
          // tablet_id
          cells[i].set_int(tablet_id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          // compaction_scn
          cells[i].set_uint64(compaction_scn_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: {
          // row_count
          cells[i].set_int(row_count_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: {
          // data_checksum
          cells[i].set_int(data_checksum_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 4: {
          // column_checksums
          cells[i].set_varchar(column_checksums_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 5: {
          // b_column_checksums
          cells[i].set_varbinary(b_column_checksums_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 6: {
          // data_checksum_type
          cells[i].set_int(data_checksum_type_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 7: {
          // co_base_snapshot_version
          cells[i].set_uint64(co_base_snapshot_version_);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
          break;
        }
      }
    }
  }

  return ret;
}

ObAllVirtualZoneMergeInfo::ObAllVirtualZoneMergeInfo()
  : is_inited_(false),
    guard_(),
    stmt_(nullptr),
    row_idx_(0),
    id_(0),
    all_merged_scn_(0),
    broadcast_scn_(0),
    frozen_scn_(0),
    is_merging_(0),
    last_merged_time_(0),
    last_merged_scn_(0),
    merge_start_time_(0),
    merge_status_(0)
{
}

ObAllVirtualZoneMergeInfo::~ObAllVirtualZoneMergeInfo()
{
  reset();
}

void ObAllVirtualZoneMergeInfo::reset()
{
  if (stmt_) {
    guard_->finalize_query(stmt_);
    stmt_ = nullptr;
  }
  is_inited_ = false;
  row_idx_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualZoneMergeInfo::inner_open()
{
  int ret = OB_SUCCESS;

  ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
  if (!guard) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to acquire connection", K(ret));
  } else {
    guard_ = std::move(guard);
    const char *select_sql =
      "SELECT id, all_merged_scn, broadcast_scn, frozen_scn, is_merging, last_merged_time, last_merged_scn, merge_start_time, merge_status "
      "FROM __all_zone_merge_info;";

    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {
      SERVER_LOG(WARN, "failed to prepare query", K(ret));
    } else {
      is_inited_ = true;
      row_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualZoneMergeInfo::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not initialized", K(ret));
  } else {
    ret = get_next_row_from_sqlite();
    if (OB_ITER_END == ret) {
      // End of result set
    } else if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));
    } else if (OB_FAIL(fill_cells())) {
      SERVER_LOG(WARN, "failed to fill cells", K(ret));
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

int ObAllVirtualZoneMergeInfo::get_next_row_from_sqlite()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));
  } else {
    ObSQLiteRowReader reader;
    ret = guard_->step_query(stmt_, reader);
    if (OB_SUCC(ret)) {
      id_ = reader.get_int64();
      all_merged_scn_ = reader.get_int64();
      broadcast_scn_ = reader.get_int64();
      frozen_scn_ = reader.get_int64();
      is_merging_ = reader.get_int64();
      last_merged_time_ = reader.get_int64();
      last_merged_scn_ = reader.get_int64();
      merge_start_time_ = reader.get_int64();
      merge_status_ = reader.get_int64();
    } else if (OB_ITER_END == ret) {
      // End of result set
    } else {
      SERVER_LOG(WARN, "failed to step query", K(ret));
    }
  }

  return ret;
}

int ObAllVirtualZoneMergeInfo::fill_cells()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0: {
          // id
          cells[i].set_int(id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          // all_merged_scn
          cells[i].set_uint64(all_merged_scn_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: {
          // broadcast_scn
          cells[i].set_uint64(broadcast_scn_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: {
          // frozen_scn
          cells[i].set_uint64(frozen_scn_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 4: {
          // is_merging
          cells[i].set_int(is_merging_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 5: {
          // last_merged_time
          cells[i].set_int(last_merged_time_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 6: {
          // last_merged_scn
          cells[i].set_uint64(last_merged_scn_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 7: {
          // merge_start_time
          cells[i].set_int(merge_start_time_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 8: {
          // merge_status
          cells[i].set_int(merge_status_);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
          break;
        }
      }
    }
  }

  return ret;
}

ObAllVirtualMergeInfo::ObAllVirtualMergeInfo()
  : is_inited_(false),
    guard_(),
    stmt_(nullptr),
    row_idx_(0),
    id_(0),
    frozen_scn_(0),
    global_broadcast_scn_(0),
    is_merge_error_(0),
    last_merged_scn_(0),
    merge_status_(0),
    error_type_(0),
    suspend_merging_(0),
    merge_start_time_(0),
    last_merged_time_(0)
{
}

ObAllVirtualMergeInfo::~ObAllVirtualMergeInfo()
{
  reset();
}

void ObAllVirtualMergeInfo::reset()
{
  if (stmt_) {
    guard_->finalize_query(stmt_);
    stmt_ = nullptr;
  }
  is_inited_ = false;
  row_idx_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualMergeInfo::inner_open()
{
  int ret = OB_SUCCESS;

  ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
  if (!guard) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to acquire connection", K(ret));
  } else {
    guard_ = std::move(guard);
    const char *select_sql =
      "SELECT id, frozen_scn, global_broadcast_scn, is_merge_error, last_merged_scn, merge_status, error_type, suspend_merging, merge_start_time, last_merged_time "
      "FROM __all_merge_info;";

    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {
      SERVER_LOG(WARN, "failed to prepare query", K(ret));
    } else {
      is_inited_ = true;
      row_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualMergeInfo::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not initialized", K(ret));
  } else {
    ret = get_next_row_from_sqlite();
    if (OB_ITER_END == ret) {
      // End of result set
    } else if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));
    } else if (OB_FAIL(fill_cells())) {
      SERVER_LOG(WARN, "failed to fill cells", K(ret));
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

int ObAllVirtualMergeInfo::get_next_row_from_sqlite()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));
  } else {
    ObSQLiteRowReader reader;
    ret = guard_->step_query(stmt_, reader);
    if (OB_SUCC(ret)) {
      id_ = reader.get_int64();
      frozen_scn_ = reader.get_int64();
      global_broadcast_scn_ = reader.get_int64();
      is_merge_error_ = reader.get_int64();
      last_merged_scn_ = reader.get_int64();
      merge_status_ = reader.get_int64();
      error_type_ = reader.get_int64();
      suspend_merging_ = reader.get_int64();
      merge_start_time_ = reader.get_int64();
      last_merged_time_ = reader.get_int64();
    } else if (OB_ITER_END == ret) {
      // End of result set
    } else {
      SERVER_LOG(WARN, "failed to step query", K(ret));
    }
  }

  return ret;
}

int ObAllVirtualMergeInfo::fill_cells()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0: {
          // id
          cells[i].set_int(id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          // frozen_scn
          cells[i].set_uint64(frozen_scn_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: {
          // global_broadcast_scn
          cells[i].set_uint64(global_broadcast_scn_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: {
          // is_merge_error
          cells[i].set_int(is_merge_error_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 4: {
          // last_merged_scn
          cells[i].set_uint64(last_merged_scn_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 5: {
          // merge_status
          cells[i].set_int(merge_status_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 6: {
          // error_type
          cells[i].set_int(error_type_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 7: {
          // suspend_merging
          cells[i].set_int(suspend_merging_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 8: {
          // merge_start_time
          cells[i].set_int(merge_start_time_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 9: {
          // last_merged_time
          cells[i].set_int(last_merged_time_);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
          break;
        }
      }
    }
  }

  return ret;
}

ObAllVirtualColumnChecksumErrorInfo::ObAllVirtualColumnChecksumErrorInfo()
  : is_inited_(false),
    guard_(),
    stmt_(nullptr),
    row_idx_(0),
    frozen_scn_(0),
    index_type_(0),
    data_table_id_(0),
    index_table_id_(0),
    data_tablet_id_(0),
    index_tablet_id_(0),
    column_id_(0),
    data_column_checksum_(0),
    index_column_checksum_(0)
{
}

ObAllVirtualColumnChecksumErrorInfo::~ObAllVirtualColumnChecksumErrorInfo()
{
  reset();
}

void ObAllVirtualColumnChecksumErrorInfo::reset()
{
  if (stmt_) {
    guard_->finalize_query(stmt_);
    stmt_ = nullptr;
  }
  is_inited_ = false;
  row_idx_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualColumnChecksumErrorInfo::inner_open()
{
  int ret = OB_SUCCESS;

  ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
  if (!guard) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to acquire connection", K(ret));
  } else {
    guard_ = std::move(guard);
    const char *select_sql =
      "SELECT frozen_scn, index_type, data_table_id, index_table_id, data_tablet_id, index_tablet_id, column_id, data_column_checksum, index_column_checksum "
      "FROM __all_column_checksum_error_info;";

    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {
      SERVER_LOG(WARN, "failed to prepare query", K(ret));
    } else {
      is_inited_ = true;
      row_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualColumnChecksumErrorInfo::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not initialized", K(ret));
  } else {
    ret = get_next_row_from_sqlite();
    if (OB_ITER_END == ret) {
      // End of result set
    } else if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));
    } else if (OB_FAIL(fill_cells())) {
      SERVER_LOG(WARN, "failed to fill cells", K(ret));
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

int ObAllVirtualColumnChecksumErrorInfo::get_next_row_from_sqlite()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));
  } else {
    ObSQLiteRowReader reader;
    ret = guard_->step_query(stmt_, reader);
    if (OB_SUCC(ret)) {
      frozen_scn_ = reader.get_int64();
      index_type_ = reader.get_int64();
      data_table_id_ = reader.get_int64();
      index_table_id_ = reader.get_int64();
      data_tablet_id_ = reader.get_int64();
      index_tablet_id_ = reader.get_int64();
      column_id_ = reader.get_int64();
      data_column_checksum_ = reader.get_int64();
      index_column_checksum_ = reader.get_int64();
    } else if (OB_ITER_END == ret) {
      // End of result set
    } else {
      SERVER_LOG(WARN, "failed to step query", K(ret));
    }
  }

  return ret;
}

int ObAllVirtualColumnChecksumErrorInfo::fill_cells()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0: {
          // frozen_scn
          cells[i].set_uint64(frozen_scn_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          // index_type
          cells[i].set_int(index_type_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: {
          // data_table_id
          cells[i].set_int(data_table_id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: {
          // index_table_id
          cells[i].set_int(index_table_id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 4: {
          // data_tablet_id
          cells[i].set_int(data_tablet_id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 5: {
          // index_tablet_id
          cells[i].set_int(index_tablet_id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 6: {
          // column_id
          cells[i].set_int(column_id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 7: {
          // data_column_checksum
          cells[i].set_int(data_column_checksum_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 8: {
          // index_column_checksum
          cells[i].set_int(index_column_checksum_);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
          break;
        }
      }
    }
  }

  return ret;
}

ObAllVirtualReservedSnapshot::ObAllVirtualReservedSnapshot()
  : is_inited_(false),
    guard_(),
    stmt_(nullptr),
    row_idx_(0),
    snapshot_type_(0),
    create_time_(0),
    snapshot_version_(0),
    status_(0)
{
}

ObAllVirtualReservedSnapshot::~ObAllVirtualReservedSnapshot()
{
  reset();
}

void ObAllVirtualReservedSnapshot::reset()
{
  if (stmt_) {
    guard_->finalize_query(stmt_);
    stmt_ = nullptr;
  }
  is_inited_ = false;
  row_idx_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualReservedSnapshot::inner_open()
{
  int ret = OB_SUCCESS;

  ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
  if (!guard) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to acquire connection", K(ret));
  } else {
    guard_ = std::move(guard);
    const char *select_sql =
      "SELECT snapshot_type, create_time, snapshot_version, status "
      "FROM __all_reserved_snapshot;";

    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {
      SERVER_LOG(WARN, "failed to prepare query", K(ret));
    } else {
      is_inited_ = true;
      row_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualReservedSnapshot::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not initialized", K(ret));
  } else {
    ret = get_next_row_from_sqlite();
    if (OB_ITER_END == ret) {
      // End of result set
    } else if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));
    } else if (OB_FAIL(fill_cells())) {
      SERVER_LOG(WARN, "failed to fill cells", K(ret));
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

int ObAllVirtualReservedSnapshot::get_next_row_from_sqlite()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));
  } else {
    ObSQLiteRowReader reader;
    ret = guard_->step_query(stmt_, reader);
    if (OB_SUCC(ret)) {
      snapshot_type_ = reader.get_int64();
      create_time_ = reader.get_int64();
      snapshot_version_ = reader.get_int64();
      status_ = reader.get_int64();
    } else if (OB_ITER_END == ret) {
      // End of result set
    } else {
      SERVER_LOG(WARN, "failed to step query", K(ret));
    }
  }

  return ret;
}

int ObAllVirtualReservedSnapshot::fill_cells()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0: {
          // snapshot_type
          cells[i].set_int(snapshot_type_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          // create_time
          cells[i].set_int(create_time_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: {
          // snapshot_version
          cells[i].set_uint64(snapshot_version_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: {
          // status
          cells[i].set_int(status_);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
          break;
        }
      }
    }
  }

  return ret;
}

ObAllVirtualServerEventHistory::ObAllVirtualServerEventHistory()
  : is_inited_(false),
    guard_(),
    stmt_(nullptr),
    row_idx_(0),
    gmt_create_(0),
    event_type_(0),
    module_(),
    event_(),
    name1_(),
    value1_(),
    name2_(),
    value2_(),
    name3_(),
    value3_(),
    name4_(),
    value4_(),
    name5_(),
    value5_(),
    name6_(),
    value6_(),
    extra_info_()
{
}

ObAllVirtualServerEventHistory::~ObAllVirtualServerEventHistory()
{
  reset();
}

void ObAllVirtualServerEventHistory::reset()
{
  if (stmt_) {
    guard_->finalize_query(stmt_);
    stmt_ = nullptr;
  }
  is_inited_ = false;
  row_idx_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualServerEventHistory::inner_open()
{
  int ret = OB_SUCCESS;

  ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
  if (!guard) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to acquire connection", K(ret));
  } else {
    guard_ = std::move(guard);
    const char *select_sql =
      "SELECT gmt_create, event_type, module, event, name1, value1, name2, value2, name3, value3, name4, value4, name5, value5, name6, value6, extra_info "
      "FROM __all_server_event_history;";

    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {
      SERVER_LOG(WARN, "failed to prepare query", K(ret));
    } else {
      is_inited_ = true;
      row_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualServerEventHistory::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not initialized", K(ret));
  } else {
    ret = get_next_row_from_sqlite();
    if (OB_ITER_END == ret) {
      // End of result set
    } else if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));
    } else if (OB_FAIL(fill_cells())) {
      SERVER_LOG(WARN, "failed to fill cells", K(ret));
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

int ObAllVirtualServerEventHistory::get_next_row_from_sqlite()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));
  } else {
    ObSQLiteRowReader reader;
    ret = guard_->step_query(stmt_, reader);
    if (OB_SUCC(ret)) {
      gmt_create_ = reader.get_int64();
      event_type_ = reader.get_int64();
      int module_len = 0;
      const char *module_str = reader.get_text(&module_len);
      if (OB_ISNULL(module_str)) {
        module_.reset();
      } else {
        module_.assign_ptr(module_str, module_len);
      }
      int event_len = 0;
      const char *event_str = reader.get_text(&event_len);
      if (OB_ISNULL(event_str)) {
        event_.reset();
      } else {
        event_.assign_ptr(event_str, event_len);
      }
      int name1_len = 0;
      const char *name1_str = reader.get_text(&name1_len);
      if (OB_ISNULL(name1_str)) {
        name1_.reset();
      } else {
        name1_.assign_ptr(name1_str, name1_len);
      }
      int value1_len = 0;
      const char *value1_str = reader.get_text(&value1_len);
      if (OB_ISNULL(value1_str)) {
        value1_.reset();
      } else {
        value1_.assign_ptr(value1_str, value1_len);
      }
      int name2_len = 0;
      const char *name2_str = reader.get_text(&name2_len);
      if (OB_ISNULL(name2_str)) {
        name2_.reset();
      } else {
        name2_.assign_ptr(name2_str, name2_len);
      }
      int value2_len = 0;
      const char *value2_str = reader.get_text(&value2_len);
      if (OB_ISNULL(value2_str)) {
        value2_.reset();
      } else {
        value2_.assign_ptr(value2_str, value2_len);
      }
      int name3_len = 0;
      const char *name3_str = reader.get_text(&name3_len);
      if (OB_ISNULL(name3_str)) {
        name3_.reset();
      } else {
        name3_.assign_ptr(name3_str, name3_len);
      }
      int value3_len = 0;
      const char *value3_str = reader.get_text(&value3_len);
      if (OB_ISNULL(value3_str)) {
        value3_.reset();
      } else {
        value3_.assign_ptr(value3_str, value3_len);
      }
      int name4_len = 0;
      const char *name4_str = reader.get_text(&name4_len);
      if (OB_ISNULL(name4_str)) {
        name4_.reset();
      } else {
        name4_.assign_ptr(name4_str, name4_len);
      }
      int value4_len = 0;
      const char *value4_str = reader.get_text(&value4_len);
      if (OB_ISNULL(value4_str)) {
        value4_.reset();
      } else {
        value4_.assign_ptr(value4_str, value4_len);
      }
      int name5_len = 0;
      const char *name5_str = reader.get_text(&name5_len);
      if (OB_ISNULL(name5_str)) {
        name5_.reset();
      } else {
        name5_.assign_ptr(name5_str, name5_len);
      }
      int value5_len = 0;
      const char *value5_str = reader.get_text(&value5_len);
      if (OB_ISNULL(value5_str)) {
        value5_.reset();
      } else {
        value5_.assign_ptr(value5_str, value5_len);
      }
      int name6_len = 0;
      const char *name6_str = reader.get_text(&name6_len);
      if (OB_ISNULL(name6_str)) {
        name6_.reset();
      } else {
        name6_.assign_ptr(name6_str, name6_len);
      }
      int value6_len = 0;
      const char *value6_str = reader.get_text(&value6_len);
      if (OB_ISNULL(value6_str)) {
        value6_.reset();
      } else {
        value6_.assign_ptr(value6_str, value6_len);
      }
      int extra_info_len = 0;
      const char *extra_info_str = reader.get_text(&extra_info_len);
      if (OB_ISNULL(extra_info_str)) {
        extra_info_.reset();
      } else {
        extra_info_.assign_ptr(extra_info_str, extra_info_len);
      }
    } else if (OB_ITER_END == ret) {
      // End of result set
    } else {
      SERVER_LOG(WARN, "failed to step query", K(ret));
    }
  }

  return ret;
}

int ObAllVirtualServerEventHistory::fill_cells()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0: {
          // gmt_create
          cells[i].set_int(gmt_create_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          // event_type
          cells[i].set_int(event_type_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: {
          // module
          cells[i].set_varchar(module_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: {
          // event
          cells[i].set_varchar(event_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 4: {
          // name1
          cells[i].set_varchar(name1_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 5: {
          // value1
          cells[i].set_varchar(value1_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 6: {
          // name2
          cells[i].set_varchar(name2_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 7: {
          // value2
          cells[i].set_varchar(value2_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 8: {
          // name3
          cells[i].set_varchar(name3_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 9: {
          // value3
          cells[i].set_varchar(value3_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 10: {
          // name4
          cells[i].set_varchar(name4_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 11: {
          // value4
          cells[i].set_varchar(value4_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 12: {
          // name5
          cells[i].set_varchar(name5_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 13: {
          // value5
          cells[i].set_varchar(value5_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 14: {
          // name6
          cells[i].set_varchar(name6_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 15: {
          // value6
          cells[i].set_varchar(value6_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 16: {
          // extra_info
          cells[i].set_varchar(extra_info_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
          break;
        }
      }
    }
  }

  return ret;
}

ObAllVirtualTenantEventHistory::ObAllVirtualTenantEventHistory()
  : is_inited_(false),
    guard_(),
    stmt_(nullptr),
    row_idx_(0),
    gmt_create_(0),
    module_(),
    event_(),
    name1_(),
    value1_(),
    name2_(),
    value2_(),
    name3_(),
    value3_(),
    name4_(),
    value4_(),
    name5_(),
    value5_(),
    name6_(),
    value6_(),
    extra_info_(),
    trace_id_(),
    cost_time_(0),
    ret_code_(0),
    error_msg_()
{
}

ObAllVirtualTenantEventHistory::~ObAllVirtualTenantEventHistory()
{
  reset();
}

void ObAllVirtualTenantEventHistory::reset()
{
  if (stmt_) {
    guard_->finalize_query(stmt_);
    stmt_ = nullptr;
  }
  is_inited_ = false;
  row_idx_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualTenantEventHistory::inner_open()
{
  int ret = OB_SUCCESS;

  ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
  if (!guard) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to acquire connection", K(ret));
  } else {
    guard_ = std::move(guard);
    const char *select_sql =
      "SELECT gmt_create, module, event, name1, value1, name2, value2, name3, value3, name4, value4, name5, value5, name6, value6, extra_info, trace_id, cost_time, ret_code, error_msg "
      "FROM __all_tenant_event_history;";

    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {
      SERVER_LOG(WARN, "failed to prepare query", K(ret));
    } else {
      is_inited_ = true;
      row_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualTenantEventHistory::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not initialized", K(ret));
  } else {
    ret = get_next_row_from_sqlite();
    if (OB_ITER_END == ret) {
      // End of result set
    } else if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));
    } else if (OB_FAIL(fill_cells())) {
      SERVER_LOG(WARN, "failed to fill cells", K(ret));
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

int ObAllVirtualTenantEventHistory::get_next_row_from_sqlite()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));
  } else {
    ObSQLiteRowReader reader;
    ret = guard_->step_query(stmt_, reader);
    if (OB_SUCC(ret)) {
      gmt_create_ = reader.get_int64();
      int module_len = 0;
      const char *module_str = reader.get_text(&module_len);
      if (OB_ISNULL(module_str)) {
        module_.reset();
      } else {
        module_.assign_ptr(module_str, module_len);
      }
      int event_len = 0;
      const char *event_str = reader.get_text(&event_len);
      if (OB_ISNULL(event_str)) {
        event_.reset();
      } else {
        event_.assign_ptr(event_str, event_len);
      }
      int name1_len = 0;
      const char *name1_str = reader.get_text(&name1_len);
      if (OB_ISNULL(name1_str)) {
        name1_.reset();
      } else {
        name1_.assign_ptr(name1_str, name1_len);
      }
      int value1_len = 0;
      const char *value1_str = reader.get_text(&value1_len);
      if (OB_ISNULL(value1_str)) {
        value1_.reset();
      } else {
        value1_.assign_ptr(value1_str, value1_len);
      }
      int name2_len = 0;
      const char *name2_str = reader.get_text(&name2_len);
      if (OB_ISNULL(name2_str)) {
        name2_.reset();
      } else {
        name2_.assign_ptr(name2_str, name2_len);
      }
      int value2_len = 0;
      const char *value2_str = reader.get_text(&value2_len);
      if (OB_ISNULL(value2_str)) {
        value2_.reset();
      } else {
        value2_.assign_ptr(value2_str, value2_len);
      }
      int name3_len = 0;
      const char *name3_str = reader.get_text(&name3_len);
      if (OB_ISNULL(name3_str)) {
        name3_.reset();
      } else {
        name3_.assign_ptr(name3_str, name3_len);
      }
      int value3_len = 0;
      const char *value3_str = reader.get_text(&value3_len);
      if (OB_ISNULL(value3_str)) {
        value3_.reset();
      } else {
        value3_.assign_ptr(value3_str, value3_len);
      }
      int name4_len = 0;
      const char *name4_str = reader.get_text(&name4_len);
      if (OB_ISNULL(name4_str)) {
        name4_.reset();
      } else {
        name4_.assign_ptr(name4_str, name4_len);
      }
      int value4_len = 0;
      const char *value4_str = reader.get_text(&value4_len);
      if (OB_ISNULL(value4_str)) {
        value4_.reset();
      } else {
        value4_.assign_ptr(value4_str, value4_len);
      }
      int name5_len = 0;
      const char *name5_str = reader.get_text(&name5_len);
      if (OB_ISNULL(name5_str)) {
        name5_.reset();
      } else {
        name5_.assign_ptr(name5_str, name5_len);
      }
      int value5_len = 0;
      const char *value5_str = reader.get_text(&value5_len);
      if (OB_ISNULL(value5_str)) {
        value5_.reset();
      } else {
        value5_.assign_ptr(value5_str, value5_len);
      }
      int name6_len = 0;
      const char *name6_str = reader.get_text(&name6_len);
      if (OB_ISNULL(name6_str)) {
        name6_.reset();
      } else {
        name6_.assign_ptr(name6_str, name6_len);
      }
      int value6_len = 0;
      const char *value6_str = reader.get_text(&value6_len);
      if (OB_ISNULL(value6_str)) {
        value6_.reset();
      } else {
        value6_.assign_ptr(value6_str, value6_len);
      }
      int extra_info_len = 0;
      const char *extra_info_str = reader.get_text(&extra_info_len);
      if (OB_ISNULL(extra_info_str)) {
        extra_info_.reset();
      } else {
        extra_info_.assign_ptr(extra_info_str, extra_info_len);
      }
      int trace_id_len = 0;
      const char *trace_id_str = reader.get_text(&trace_id_len);
      if (OB_ISNULL(trace_id_str)) {
        trace_id_.reset();
      } else {
        trace_id_.assign_ptr(trace_id_str, trace_id_len);
      }
      cost_time_ = reader.get_int64();
      ret_code_ = reader.get_int64();
      int error_msg_len = 0;
      const char *error_msg_str = reader.get_text(&error_msg_len);
      if (OB_ISNULL(error_msg_str)) {
        error_msg_.reset();
      } else {
        error_msg_.assign_ptr(error_msg_str, error_msg_len);
      }
    } else if (OB_ITER_END == ret) {
      // End of result set
    } else {
      SERVER_LOG(WARN, "failed to step query", K(ret));
    }
  }

  return ret;
}

int ObAllVirtualTenantEventHistory::fill_cells()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0: {
          // gmt_create
          cells[i].set_int(gmt_create_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          // module
          cells[i].set_varchar(module_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: {
          // event
          cells[i].set_varchar(event_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: {
          // name1
          cells[i].set_varchar(name1_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 4: {
          // value1
          cells[i].set_varchar(value1_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 5: {
          // name2
          cells[i].set_varchar(name2_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 6: {
          // value2
          cells[i].set_varchar(value2_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 7: {
          // name3
          cells[i].set_varchar(name3_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 8: {
          // value3
          cells[i].set_varchar(value3_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 9: {
          // name4
          cells[i].set_varchar(name4_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 10: {
          // value4
          cells[i].set_varchar(value4_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 11: {
          // name5
          cells[i].set_varchar(name5_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 12: {
          // value5
          cells[i].set_varchar(value5_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 13: {
          // name6
          cells[i].set_varchar(name6_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 14: {
          // value6
          cells[i].set_varchar(value6_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 15: {
          // extra_info
          cells[i].set_varchar(extra_info_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 16: {
          // trace_id
          cells[i].set_varchar(trace_id_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 17: {
          // cost_time
          cells[i].set_int(cost_time_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 18: {
          // ret_code
          cells[i].set_int(ret_code_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 19: {
          // error_msg
          cells[i].set_varchar(error_msg_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
          break;
        }
      }
    }
  }

  return ret;
}

ObAllVirtualRootserviceJob::ObAllVirtualRootserviceJob()
  : is_inited_(false),
    guard_(),
    stmt_(nullptr),
    row_idx_(0),
    job_id_(0),
    gmt_create_(0),
    gmt_modified_(0),
    job_type_(),
    job_status_(),
    result_code_(0)
{
}

ObAllVirtualRootserviceJob::~ObAllVirtualRootserviceJob()
{
  reset();
}

void ObAllVirtualRootserviceJob::reset()
{
  if (stmt_) {
    guard_->finalize_query(stmt_);
    stmt_ = nullptr;
  }
  is_inited_ = false;
  row_idx_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualRootserviceJob::inner_open()
{
  int ret = OB_SUCCESS;

  ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
  if (!guard) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to acquire connection", K(ret));
  } else {
    guard_ = std::move(guard);
    const char *select_sql =
      "SELECT job_id, gmt_create, gmt_modified, job_type, job_status, result_code "
      "FROM __all_rootservice_job;";

    if (OB_FAIL(guard_->prepare_query(select_sql, nullptr, stmt_))) {
      SERVER_LOG(WARN, "failed to prepare query", K(ret));
    } else {
      is_inited_ = true;
      row_idx_ = 0;
    }
  }

  return ret;
}

int ObAllVirtualRootserviceJob::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not initialized", K(ret));
  } else {
    ret = get_next_row_from_sqlite();
    if (OB_ITER_END == ret) {
      // End of result set
    } else if (OB_FAIL(ret)) {
      SERVER_LOG(WARN, "failed to get next row from sqlite", K(ret));
    } else if (OB_FAIL(fill_cells())) {
      SERVER_LOG(WARN, "failed to fill cells", K(ret));
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

int ObAllVirtualRootserviceJob::get_next_row_from_sqlite()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "statement is null", K(ret), KP(stmt_));
  } else {
    ObSQLiteRowReader reader;
    ret = guard_->step_query(stmt_, reader);
    if (OB_SUCC(ret)) {
      job_id_ = reader.get_int64();
      gmt_create_ = reader.get_int64();
      gmt_modified_ = reader.get_int64();
      int job_type_len = 0;
      const char *job_type_str = reader.get_text(&job_type_len);
      if (OB_ISNULL(job_type_str)) {
        job_type_.reset();
      } else {
        job_type_.assign_ptr(job_type_str, job_type_len);
      }
      int job_status_len = 0;
      const char *job_status_str = reader.get_text(&job_status_len);
      if (OB_ISNULL(job_status_str)) {
        job_status_.reset();
      } else {
        job_status_.assign_ptr(job_status_str, job_status_len);
      }
      result_code_ = reader.get_int64();
    } else if (OB_ITER_END == ret) {
      // End of result set
    } else {
      SERVER_LOG(WARN, "failed to step query", K(ret));
    }
  }

  return ret;
}

int ObAllVirtualRootserviceJob::fill_cells()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = output_column_ids_.count();
  ObObj *cells = cur_row_.cells_;

  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case OB_APP_MIN_COLUMN_ID + 0: {
          // job_id
          cells[i].set_int(job_id_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 1: {
          // gmt_create
          cells[i].set_int(gmt_create_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 2: {
          // gmt_modified
          cells[i].set_int(gmt_modified_);
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 3: {
          // job_type
          cells[i].set_varchar(job_type_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 4: {
          // job_status
          cells[i].set_varchar(job_status_);
          cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case OB_APP_MIN_COLUMN_ID + 5: {
          // result_code
          cells[i].set_int(result_code_);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
          break;
        }
      }
    }
  }

  return ret;
}

} // namespace observer
} // namespace oceanbase
