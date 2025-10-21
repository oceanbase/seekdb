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

#define USING_LOG_PREFIX COMMON

#include "lib/stat/ob_diagnostic_info_container.h"
#include "lib/stat/ob_diagnostic_info_util.h"
#include "lib/container/ob_vector.h"
#include "share/ash/ob_active_sess_hist_list.h"

namespace oceanbase
{
namespace common
{

int64_t __attribute__((weak, noinline)) get_mtl_id()
{
  return OB_INVALID_TENANT_ID;
}
ObDiagnosticInfoContainer *__attribute__((weak, noinline)) get_di_container()
{
  return nullptr;
}

uint64_t __attribute__((weak, noinline)) lib_get_cpu_khz()
{
  return 0;
}

void __attribute__((weak, noinline)) lib_mtl_switch(int64_t tenant_id, std::function<void(int)> fn)
{
  UNUSED(tenant_id);
  fn(OB_NOT_IMPLEMENT);
}

int64_t __attribute__((weak, noinline)) lib_mtl_cpu_count()
{
  return 1;
}


share::ObActiveSessHistList *__attribute__((weak, noinline)) lib_get_ash_list_instance()
{
  return nullptr;
}

} /* namespace common */
} /* namespace oceanbase */
