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

#include "ob_normal_adapter_iter.h"

namespace oceanbase
{   
namespace table
{

int ObHbaseNormalCellIter::get_next_cell(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (!is_opened_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iterator is not opened", K(ret));
  } else if (OB_FAIL(tb_row_iter_.get_next_row(row))) {
    if (ret == OB_ITER_END) {
      LOG_DEBUG("iterator is end", K(ret));
    } else{
      LOG_WARN("fail to get next cell", K(ret));
    }
  }
  return ret;
}
 
} // end of namespace table
} // end of namespace oceanbase
