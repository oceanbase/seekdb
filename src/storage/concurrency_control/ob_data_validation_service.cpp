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

namespace oceanbase
{
namespace concurrency_control
{


void ObDataValidationService::set_delay_resource_recycle(const ObLSID ls_id)
{
  int ret = OB_SUCCESS;
  ObLSHandle handle;
  ObLSService *ls_service = MTL(ObLSService*);
  ObLS *ls = nullptr;
  const bool need_delay_opt = GCONF._delay_resource_recycle_after_correctness_issue;

  if (OB_LIKELY(!need_delay_opt)) {
    // do nothing
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(ls_id));
  } else if (OB_FAIL(ls_service->get_ls(ls_id, handle, ObLSGetMod::TXSTORAGE_MOD))) {
    if (OB_LS_NOT_EXIST != ret) {
      TRANS_LOG(DEBUG, "get log stream failed", K(ls_id), K(ret));
    }
  } else if (OB_ISNULL(ls = handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "get log stream failed", K(ls_id), K(ret));
  } else {
    ls->set_delay_resource_recycle();
  }
}

} // namespace concurrency_control
} // namespace oceanbase
