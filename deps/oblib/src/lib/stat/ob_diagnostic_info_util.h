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

#ifndef OB_DIAGNOSTIC_INFO_UTIL_H_
#define OB_DIAGNOSTIC_INFO_UTIL_H_

#include "lib/stat/ob_diagnostic_info_container.h"
#include "lib/stat/ob_diagnose_info.h"
#include "lib/stat/ob_di_cache.h"
#include "share/ash/ob_active_sess_hist_list.h"

namespace oceanbase
{
namespace common
{

extern int64_t get_mtl_id();
extern ObDiagnosticInfoContainer *get_di_container();
extern uint64_t lib_get_cpu_khz();
extern void lib_mtl_switch(int64_t tenant_id, std::function<void(int)> fn);
extern int64_t lib_mtl_cpu_count();
extern share::ObActiveSessHistList* lib_get_ash_list_instance();

#define LIB_MTL_ID() get_mtl_id()

#define MTL_DI_CONTAINER() get_di_container()

}  // namespace common
}  // namespace oceanbase

#endif /* OB_DIAGNOSTIC_INFO_UTIL_H_ */
