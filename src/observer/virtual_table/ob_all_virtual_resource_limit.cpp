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

#include "observer/virtual_table/ob_all_virtual_resource_limit.h"

using namespace oceanbase::common;
using namespace oceanbase::storage;
using namespace oceanbase::share;
namespace oceanbase
{
namespace observer
{

ObResourceLimitTable::ObResourceLimitTable()
  : ObVirtualTableScannerIterator(),
    iter_()
{
}

ObResourceLimitTable::~ObResourceLimitTable()
{
  reset();
}

void ObResourceLimitTable::reset()
{
  iter_.reset();
  ObVirtualTableScannerIterator::reset();
}

int ObResourceLimitTable::get_next_resource_info_(ObResourceInfo &info)
{
  int ret = OB_SUCCESS;
  if (!iter_.is_ready()
      && OB_FAIL(iter_.set_ready())) {
    LOG_WARN("iterator is not ready", K(ret));
  } else if (OB_FAIL(iter_.get_next(info))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("get next resource info failed", K(ret));
    }
  }
  return ret;
}

int ObResourceLimitTable::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  ObResourceInfo info;
  if (NULL == allocator_) {
    ret = OB_NOT_INIT;
    LOG_WARN("allocator_ shouldn't be NULL", K(allocator_), K(ret));
  } else if (FALSE_IT(start_to_read_ = true)) {
  } else if (OB_FAIL(get_next_resource_info_(info))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("get_next_resource_info failed", K(ret));
    }
  } else {
    const int64_t col_count = output_column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case RESOURCE_NAME: {
          cur_row_.cells_[i].set_varchar(get_logic_res_type_name(iter_.get_curr_type()));
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        }
        case CURRENT_UTILIZATION: {
          cur_row_.cells_[i].set_int(info.curr_utilization_);
          break;
        }
        case MAX_UTILIZATION:
          cur_row_.cells_[i].set_int(info.max_utilization_);
          break;
        case RSERVED_VALUE:
          cur_row_.cells_[i].set_int(info.reserved_value_);
          break;
        case LIMIT_VALUE:
          cur_row_.cells_[i].set_int(info.min_constraint_value_);
          break;
        case EFFECTIVE_LIMIT_TYPE:
          cur_row_.cells_[i].set_varchar(get_constraint_type_name(info.min_constraint_type_));
          cur_row_.cells_[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid col_id", K(ret), K(col_id));
          break;
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }
  return ret;
}

} // end namespace observer
} // end namespace oceanbase
