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

#define USING_LOG_PREFIX SHARE

#include "lib/ash/ob_ash_bkgd_sess_inactive_guard.h"
#include "lib/ash/ob_active_session_guard.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "lib/stat/ob_diagnostic_info_util.h"

using namespace oceanbase::common;

ObBKGDSessInActiveGuard::ObBKGDSessInActiveGuard()
{
  ObDiagnosticInfo *di = ObLocalDiagnosticInfo::get();
  if (OB_NOT_NULL(di)) {
    need_record_ = true;
    prev_stat_ = di->get_ash_stat().is_active_session_;
    di->get_ash_stat().set_sess_inactive();
  } else {
    need_record_ = false;
  }
}
ObBKGDSessInActiveGuard::~ObBKGDSessInActiveGuard()
{
  if (need_record_) {
    if (prev_stat_) {
      GET_DIAGNOSTIC_INFO->get_ash_stat().set_sess_active();
    } else {
      GET_DIAGNOSTIC_INFO->get_ash_stat().set_sess_inactive();
    }
  }
}
