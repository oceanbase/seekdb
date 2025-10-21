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

#ifndef OB_DI_UTIL_H_
#define OB_DI_UTIL_H_

#include "lib/stat/ob_diagnostic_info_container.h"
#include "lib/stat/ob_diagnose_info.h"
#include "lib/stat/ob_di_cache.h"

namespace oceanbase
{
namespace share
{

class ObDiagnosticInfoUtil
{
public:
  static int get_the_diag_info(int64_t session_id, ObDISessionCollect &diag_infos);
  static int get_all_diag_info(ObIArray<std::pair<uint64_t, ObDISessionCollect>> &diag_infos, int64_t tenant_id = OB_SYS_TENANT_ID);
  static int get_the_diag_info(uint64_t tenant_id, ObDiagnoseTenantInfo &diag_info);
  static int get_group_diag_info(uint64_t tenant_id,
      ObArray<std::pair<int64_t, common::ObDiagnoseTenantInfo>> &diag_infos,
      common::ObIAllocator *alloc);

private:
  ObDiagnosticInfoUtil() = default;
  ~ObDiagnosticInfoUtil() = default;
  DISABLE_COPY_ASSIGN(ObDiagnosticInfoUtil);
};

}  // namespace share
}  // namespace oceanbase

#endif /* OB_DI_UTIL_H_ */
