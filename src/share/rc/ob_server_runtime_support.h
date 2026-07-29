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

#ifndef OCEANBASE_SHARE_RC_OB_SERVER_RUNTIME_SUPPORT_H_
#define OCEANBASE_SHARE_RC_OB_SERVER_RUNTIME_SUPPORT_H_

#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
namespace share
{

// Set after the server-owned module graph has started. This is a construction
// barrier, not a database switch or thread-local context.
inline bool g_modules_ready = false;

class ObModuleReadyGuard
{
public:
  int enter() const
  {
    return g_modules_ready ? common::OB_SUCCESS : common::OB_NOT_INIT;
  }
};

#define MAKE_MODULE_READY_GUARD(guard) \
  ::oceanbase::share::ObModuleReadyGuard guard

// Keep the for-once structure so existing break/continue behavior is retained.
#define MODULES_READY_SCOPE \
  for (int64_t _mod_loop = 0; _mod_loop == 0; ++_mod_loop) \
    if (OB_LIKELY(::oceanbase::share::g_modules_ready))

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_RC_OB_SERVER_RUNTIME_SUPPORT_H_
