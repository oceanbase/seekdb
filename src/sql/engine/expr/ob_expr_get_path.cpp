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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_get_path.h"
namespace oceanbase
{
using namespace common;
namespace sql
{

int ObDataAccessPathExtraInfo::deep_copy(common::ObIAllocator &allocator,
                                         const ObExprOperatorType type,
                                         ObIExprExtraInfo *&copied_info) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObExprExtraInfoFactory::alloc(allocator, type, copied_info))) {
    LOG_WARN("Failed to allocate memory for ObExprOracleLRpadInfo", K(ret));
  } else if (OB_ISNULL(copied_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("extra_info should not be nullptr", K(ret));
  } else {
    ObDataAccessPathExtraInfo *other = static_cast<ObDataAccessPathExtraInfo *>(copied_info);
    if (OB_FAIL(ob_write_string(allocator, data_access_path_, other->data_access_path_))) {
      LOG_WARN("fail to write string", K(ret));
    }
  }
  return ret;
}


OB_SERIALIZE_MEMBER(ObDataAccessPathExtraInfo, data_access_path_);

}
}
