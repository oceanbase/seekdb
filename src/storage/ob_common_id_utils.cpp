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

#define USING_LOG_PREFIX STORAGE

#include "ob_common_id_utils.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx/ob_unique_id_service.h" // ObUniqueIDService

namespace oceanbase
{
using namespace common;
using namespace share;
namespace storage
{
int ObCommonIDUtils::gen_unique_id(ObCommonID &id)
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  const int64_t DEFAULT_TIMEOUT = GCONF.rpc_timeout;
  int64_t unique_id = ObCommonID::INVALID_ID;

  id.reset();

  if (OB_UNLIKELY(false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invaild tenant id", KR(ret));
  } else if (OB_FAIL(share::ObShareUtil::set_default_timeout_ctx(ctx, DEFAULT_TIMEOUT))) {
    LOG_WARN("set default timeout ctx fail", KR(ret), K(DEFAULT_TIMEOUT));
  } else if (OB_FAIL(share::g_mp->unique_id_service()->gen_unique_id(unique_id,
      ctx.get_timeout()))) {
    LOG_WARN("gen_unique_id failed", KR(ret), K(ctx));
  } else {
    id = ObCommonID(unique_id);
  }

  return ret;
}

int ObCommonIDUtils::gen_unique_id_by_rpc(ObCommonID &id)
{
  int ret = OB_SUCCESS;
  // seekdb single-node: all LS leaders are local, just call gen_unique_id directly.
  // Switch tenant context so gen_unique_id's sys tenant check passes.
  MOD_SCOPE {
    if (OB_FAIL(gen_unique_id(id))) {
      LOG_WARN("gen_unique_id local call failed", KR(ret));
    }
  }
  return ret;
}


} // end namespace storage
} // end namespace oceanbase
