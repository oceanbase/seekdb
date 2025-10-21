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
#include "ob_table_batch_common.h"

using namespace oceanbase::observer;
using namespace oceanbase::common;
using namespace oceanbase::table;
using namespace oceanbase::share;

int ObTableBatchCtx::check_legality()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(trans_param_) || OB_ISNULL(credential_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null input", K(ret), KP_(trans_param), KP_(credential));
  } else if (tablet_ids_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet ids is empty", K(ret));
  }

  return ret;
}
