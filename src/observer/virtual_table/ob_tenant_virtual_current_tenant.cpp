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

#include "observer/virtual_table/ob_tenant_virtual_current_tenant.h"
#include "sql/printer/ob_schema_printer.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
namespace oceanbase
{
namespace observer
{

ObTenantVirtualCurrentTenant::ObTenantVirtualCurrentTenant()
    : ObVirtualTableScannerIterator(),
      sql_proxy_(NULL)
{
}

ObTenantVirtualCurrentTenant::~ObTenantVirtualCurrentTenant()
{
}

void ObTenantVirtualCurrentTenant::reset()
{
  sql_proxy_ = NULL;
  ObVirtualTableScannerIterator::reset();
}

int ObTenantVirtualCurrentTenant::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  const ObTenantSchema *tenant_schema = NULL;
  
  const int64_t col_count = output_column_ids_.count();
  int64_t pos = 0;
  if (OB_UNLIKELY(NULL == allocator_
                  || NULL == schema_guard_
                  || NULL == session_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN,
               "data member is not init", K(ret), K(allocator_), K(schema_guard_), K(session_));
  } else if (OB_UNLIKELY(cur_row_.count_ < output_column_ids_.count())) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN,
                   "cells count is less than output column count",
                   K(ret),
                   K(cur_row_.count_),
                   K(output_column_ids_.count()));
  } else {
    if (!start_to_read_) {
      // fill scanner
      if (OB_SUCC(ret)) {
        ObObj *cells = NULL;
        char *create_stmt = NULL;
        if (OB_UNLIKELY(NULL == (create_stmt = static_cast<char *>(allocator_->alloc(OB_MAX_VARCHAR_LENGTH))))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          SERVER_LOG(ERROR, "fail to alloc memory", K(ret));
        } else if (OB_FAIL(schema_guard_->get_tenant_info(tenant_schema))) {
          SERVER_LOG(WARN, "get tenant info failed", K(ret));
        } else if (OB_ISNULL(tenant_schema)) {
          ret = OB_TENANT_NOT_EXIST;
          SERVER_LOG(WARN, "Unknow tenant", K(ret));
        } else if (OB_ISNULL(cells = cur_row_.cells_)) {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "cur row cell is NULL", K(ret));
        } else {
          uint64_t cell_idx = 0;
          ObSchemaPrinter schema_printer(*schema_guard_);
          for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
            uint64_t col_id = output_column_ids_.at(i);
            switch(col_id) {//tenant_name
              case OB_APP_MIN_COLUMN_ID: {
                cells[cell_idx].set_int(static_cast<int64_t>(1));
                break;
              }
              case OB_APP_MIN_COLUMN_ID + 1: {
                cells[cell_idx].set_varchar(ObString::make_string(tenant_schema->get_tenant_name()));
                cells[cell_idx].set_collation_type(
                    ObCharset::get_default_collation(ObCharset::get_default_charset()));
                break;
              }
              case OB_APP_MIN_COLUMN_ID + 2: {//create_stmt
                if (OB_FAIL(schema_printer.print_tenant_definition(sql_proxy_,
                                                                    create_stmt,
                                                                    OB_MAX_VARCHAR_LENGTH,
                                                                    pos,
                                                                    false/*is_agent_mode*/))) {
                  SERVER_LOG(WARN, "print tenant definition fail", K(ret));
                } else {
                  cells[cell_idx].set_varchar(ObString::make_string(create_stmt));
                  cells[cell_idx].set_collation_type(
                      ObCharset::get_default_collation(ObCharset::get_default_charset()));
                }
                break;
              }
              default: {
                ret = OB_ERR_UNEXPECTED;
                SERVER_LOG(WARN, "invalid column id", K(ret), K(cell_idx),
                           K(i), K(output_column_ids_), K(col_id));
                break;
              }
            } //switch
            if (OB_SUCC(ret)) {
              cell_idx++;
            }
          } // for
          if (OB_UNLIKELY(OB_SUCCESS == ret && OB_SUCCESS != (ret = scanner_.add_row(cur_row_)))) {
            SERVER_LOG(WARN, "fail to add row", K(ret), K(cur_row_));
          } else {
            scanner_it_ = scanner_.begin();
            start_to_read_ = true;
          }
        }
      }
    } // if (!start_to_read_)
    if (OB_SUCCESS == ret && start_to_read_) {
      if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          SERVER_LOG(WARN, "fail to get next row", K(ret));
        }
      } else {
        row = &cur_row_;
      }
    }
  }
  return ret;
}
} // observer
} // namespace
