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

#ifndef OCEANBASE_LOGSERVICE_LOG_SERVICE_GUARD_
#define OCEANBASE_LOGSERVICE_LOG_SERVICE_GUARD_
#include "lib/utility/ob_print_utils.h"                 // TO_STRING_KV
#include "lib/utility/ob_macro_utils.h"                 // DISALLOW_COPY_AND_ASSIGN

namespace oceanbase
{
namespace palf
{
class IPalfHandleImpl;
class IPalfEnvImpl;
class PalfHandleImpl;
class PalfEnvImpl;

struct IPalfHandleImplGuard
{
  IPalfHandleImplGuard();
  ~IPalfHandleImplGuard();
  bool is_valid() const;
  void reset();
  IPalfHandleImpl *get_palf_handle_impl() const { return palf_handle_impl_; }
  TO_STRING_KV(K_(palf_id), KP_(palf_handle_impl), KP_(palf_env_impl));

  int64_t  palf_id_;
  IPalfHandleImpl *palf_handle_impl_;
  IPalfEnvImpl *palf_env_impl_;
private:
  DISALLOW_COPY_AND_ASSIGN(IPalfHandleImplGuard);
};
} // end namespace palf
} // end namespace oceanbase

#endif
