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

#include "ob_smart_var.h"
#include "lib/rc/context.h"
#include "lib/utility/ob_tracepoint.h"

namespace oceanbase
{
namespace common
{

ERRSIM_POINT_DEF(ERRSIM_SMART_VAR);

void *__attribute__((weak)) smart_alloc(const int64_t nbyte, const char *label)
{
  int ret = ERRSIM_SMART_VAR ? : OB_SUCCESS;
  ObMemAttr attr;
  attr.label_ = label;
  return OB_SUCCESS == ret ? lib::ctxalf(nbyte, attr) : nullptr;
}

void __attribute__((weak)) smart_free(void *ptr)
{
  lib::ctxfree(ptr);
}

} // namespace common
} // namespace oceanbase
