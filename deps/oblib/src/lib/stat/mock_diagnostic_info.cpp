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

#define private public
#define protected public
#include "deps/oblib/src/lib/ob_lib_config.h"
#undef private
#undef protected

namespace oceanbase
{

// ATTENTION: add this cpp file as library only if compiled target doesn't need diagnose. like unittest or cdc.

__attribute__((constructor)) void init_diagnostic_info()
{
  // lib::ObLibConfig::enable_diagnose_info_ = false;
  lib::ObPerfModeGuard::PERF_MODE_VALUE = true;
  lib::ObPerfModeGuard::get_tl_instance() = true;
}

} /* namespace oceanbase */
