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

#ifndef OB_UNITTEST_DIAGNOSTIC_INFO_UTIL_H_
#define OB_UNITTEST_DIAGNOSTIC_INFO_UTIL_H_

#define private public
#define protected public
#include "deps/oblib/src/lib/ob_lib_config.h"
#undef private
#undef protected

namespace oceanbase
{
namespace lib
{

class ObUnitTestEnableDiagnoseGuard
{
  friend class common::ObBackGroundSessionGuard;
  friend class common::ObDiagnosticInfoSwitchGuard;
  friend bool is_diagnose_info_enabled();
  friend bool is_trace_log_enabled();
public:
  explicit ObUnitTestEnableDiagnoseGuard() : old_value_(ObPerfModeGuard::get_tl_instance())
  {
    ObPerfModeGuard::get_tl_instance() = false;
  }
  ~ObUnitTestEnableDiagnoseGuard()
  {
    ObPerfModeGuard::get_tl_instance() = old_value_;
  }
private:
  bool old_value_;
};


} /* namespace lib */
} /* namespace oceanbase */

#endif /* OB_UNITTEST_DIAGNOSTIC_INFO_UTIL_H_ */
