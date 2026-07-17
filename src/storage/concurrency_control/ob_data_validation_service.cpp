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

#include "storage/concurrency_control/ob_data_validation_service.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace concurrency_control
{


void ObDataValidationService::set_delay_resource_recycle()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  const bool need_delay_opt = GCONF._delay_resource_recycle_after_correctness_issue;

  if (OB_LIKELY(!need_delay_opt)) {
    // do nothing
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    if (OB_LS_NOT_EXIST != ret) {
      TRANS_LOG(DEBUG, "get log stream failed", K(ret));
    }
  } else {
    ls->set_delay_resource_recycle();
  }
}

} // namespace concurrency_control
} // namespace oceanbase
