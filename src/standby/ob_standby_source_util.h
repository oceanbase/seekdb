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

#ifndef OCEANBASE_STANDBY_OB_STANDBY_SOURCE_UTIL_H_
#define OCEANBASE_STANDBY_OB_STANDBY_SOURCE_UTIL_H_

#include "lib/net/ob_addr.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace standby
{

class StandbySourceParser final
{
public:
  // Physical standby accepts either:
  //   ip:rpc_port
  //   SERVICE=ip:rpc_port[;ip:rpc_port...] [USER=...] [PASSWORD=...]
  static int get_first_service_addr(
      const common::ObString &log_restore_source,
      common::ObAddr &addr);
};

} // namespace standby
} // namespace oceanbase

#endif // OCEANBASE_STANDBY_OB_STANDBY_SOURCE_UTIL_H_
