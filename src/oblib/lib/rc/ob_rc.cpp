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

#include "ob_rc.h"
#include "lib/rc/context.h"

namespace oceanbase
{
namespace lib
{
// The server may override this weak symbol to select a resource owner.
uint64_t __attribute__ ((weak)) current_resource_owner_id()
{
  return common::OB_SERVER_RUNTIME_ID;
}
} // end of namespace lib
} // end of namespace oceanbase
