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
// deep_copy_obj / deep_copy_objparam: operates on ObObj/ObObjParam(DB-domain types), implementation moved down to common。
// declaration remains in lib/utility/utility.h(historical API location), only the definition lives here, lib no longer depends on common/object。
#define USING_LOG_PREFIX COMMON
#include "lib/utility/utility.h"
#include "common/object/ob_object.h"

namespace oceanbase
{
namespace common
{

int deep_copy_obj(ObIAllocator &allocator, const ObObj &src, ObObj &dst)
{
  int ret = OB_SUCCESS;
  if (!src.need_deep_copy()) {
    dst = src;
  } else {
    char *buf = NULL;
    int64_t size = src.get_deep_copy_size();
    int64_t pos = 0;
    if (size > 0) {
      if (NULL == (buf = static_cast<char *>(allocator.alloc(size)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("Fail to allocate memory, ", K(size), K(ret));
      } else if (OB_FAIL(dst.deep_copy(src, buf, size, pos))){
      } else { }//do nothing
    } else {
      dst = src;
    }
  }
  return ret;
}

int deep_copy_objparam(ObIAllocator &allocator, const ObObjParam &src, ObObjParam &dst)
{
  int ret = OB_SUCCESS;
  if (!src.need_deep_copy()) {
    dst = src;
  } else if (OB_FAIL(deep_copy_obj(allocator, src, dst))) {
  } else {
    dst.set_accuracy(src.get_accuracy());
    dst.unset_result_flag(dst.get_result_flag());
    dst.set_result_flag(src.get_result_flag());
    dst.set_param_flag(src.get_param_flag());
    dst.set_param_meta(src.get_param_meta());
  }
  return ret;
}


} // namespace common
} // namespace oceanbase
