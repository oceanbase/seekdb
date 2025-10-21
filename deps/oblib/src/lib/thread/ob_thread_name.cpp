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

#include "lib/thread/ob_thread_name.h"
#include <sys/prctl.h>
#include "lib/ob_define.h"
#include "lib/stat/ob_diagnostic_info_guard.h"

namespace oceanbase
{
namespace lib
{

void set_thread_name_inner(const char* name)
{
  prctl(PR_SET_NAME, name);
}

void set_thread_name(const char* type, uint64_t idx)
{
  char *name = ob_get_tname();
  uint64_t tenant_id = ob_get_tenant_id();
  char *ori_tname = ob_get_origin_thread_name();
  STRNCPY(ori_tname, type, oceanbase::OB_THREAD_NAME_BUF_LEN);
  if (tenant_id == 0) {
    snprintf(name, OB_THREAD_NAME_BUF_LEN, "%s%ld", type, idx);
  } else {
    snprintf(name, OB_THREAD_NAME_BUF_LEN, "T%ld_%s%ld", tenant_id, type, idx);
  }
  ObLocalDiagnosticInfo::set_thread_name(tenant_id, type);
  set_thread_name_inner(name);
}

void set_thread_name(const char* type)
{
  char *name = ob_get_tname();
  uint64_t tenant_id = ob_get_tenant_id();
  char *ori_tname = ob_get_origin_thread_name();
  STRNCPY(ori_tname, type, oceanbase::OB_THREAD_NAME_BUF_LEN);
  if (tenant_id == 0) {
    snprintf(name, OB_THREAD_NAME_BUF_LEN, "%s", type);
  } else {
    snprintf(name, OB_THREAD_NAME_BUF_LEN, "T%ld_%s", tenant_id, type);
  }
  ObLocalDiagnosticInfo::set_thread_name(tenant_id, type);
  set_thread_name_inner(name);
}

}; // end namespace lib
}; // end namespace oceanbase
