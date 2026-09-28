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

#include "ob_all_virtual_sys_parameter_stat.h"
#include "config_bridge.h"
#include "observer/ob_server_utils.h"
#include "src/sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;

namespace observer
{
namespace
{
int set_string_cell(ObObj &cell, const rust::String &text, ObIAllocator &allocator)
{
  ObString copied;
  int ret = ob_write_string(allocator,
      ObString(static_cast<int32_t>(text.size()), text.data()), copied);
  if (OB_SUCC(ret)) {
    cell.set_varchar(copied);
    cell.set_collation_type(
        ObCharset::get_default_collation(ObCharset::get_default_charset()));
  }
  return ret;
}
} // namespace

ObAllVirtualSysParameterStat::ObAllVirtualSysParameterStat()
    : ObVirtualTableIterator(), index_(0)
{
}

ObAllVirtualSysParameterStat::~ObAllVirtualSysParameterStat()
{
  reset();
}

int ObAllVirtualSysParameterStat::inner_open()
{
  int ret = OB_SUCCESS;
  index_ = 0;
  return ret;
}

void ObAllVirtualSysParameterStat::reset()
{
  index_ = 0;
}

int ObAllVirtualSysParameterStat::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(inner_sys_get_next_row(row))) {
  }
  return ret;
}

int ObAllVirtualSysParameterStat::inner_sys_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (index_ >= config::parameter_count()) {
    ret = OB_ITER_END;
  } else {
    ObObj *cells = cur_row_.cells_;
    if (OB_UNLIKELY(NULL == cells)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
    } else {
      const config::ParameterRow parameter = config::parameter_row(index_);
      for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
        const uint64_t col_id = output_column_ids_.at(i);
        switch (col_id) {
        case SERVER_TYPE: {
            cells[i].set_varchar("observer");
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
        case NAME: {
            ret = set_string_cell(cells[i], parameter.name, *allocator_);
            break;
          }
        case DATA_TYPE: {
            ret = set_string_cell(cells[i], parameter.data_type, *allocator_);
            break;
          }
        case VALUE: {
            ret = set_string_cell(cells[i], parameter.value, *allocator_);
            break;
          }
        case INFO: {
            ret = set_string_cell(cells[i], parameter.info, *allocator_);
            break;
          }
        case SECTION: {
            ret = set_string_cell(cells[i], parameter.section, *allocator_);
            break;
          }
        case SCOPE: {
            ret = set_string_cell(cells[i], parameter.scope, *allocator_);
            break;
          }
        case SOURCE: {
           ret = set_string_cell(cells[i], parameter.source, *allocator_);
           break;
          }
        case EDIT_LEVEL: {
           ret = set_string_cell(cells[i], parameter.edit_level, *allocator_);
           break;
          }
        case DEFAULT_VALUE: {
            ret = set_string_cell(cells[i], parameter.default_value, *allocator_);
            break;
          }
        case ISDEFAULT: {
            const ObString value(static_cast<int32_t>(parameter.value.size()), parameter.value.data());
            const ObString default_value(static_cast<int32_t>(parameter.default_value.size()), parameter.default_value.data());
            int isdefault = value.case_compare(default_value) == 0 ? 1 : 0;
            cells[i].set_int(isdefault);
            break;
          }
        default : {
            // TODO: Version compatibility, extra columns do not cause errors
            // ret = OB_ERR_UNEXPECTED;
            // SERVER_LOG(WARN, "unexpected column id", K(col_id), K(i), K(ret));
	    cells[i].set_null();
            break;
          }
        }
      } // end for
      if (OB_SUCC(ret)) {
        row = &cur_row_;
        ++index_;
      }
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
