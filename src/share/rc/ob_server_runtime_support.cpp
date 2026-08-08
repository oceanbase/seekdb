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

#include "share/rc/ob_server_runtime_support.h"
#include "share/rc/ob_server_runtime.h"
#include "share/roaringbitmap/ob_rb_memory_mgr.h"

namespace oceanbase
{
namespace common
{

ObRbMemMgr *__attribute__((used)) get_rb_mem_mgr()
{
  return ::oceanbase::share::server_service<::oceanbase::common::ObRbMemMgr>();
}

} // namespace common
} // namespace oceanbase
