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

#ifndef OCEANBASE_OBSERVER_OB_WIN_SERVICE_H_
#define OCEANBASE_OBSERVER_OB_WIN_SERVICE_H_

#ifdef _WIN32

#include <windows.h>

namespace oceanbase {
namespace observer {

static constexpr const char *OB_DEFAULT_SERVICE_NAME = "seekdb";
static constexpr const char *OB_SERVICE_DISPLAY_NAME = "SeekDB Database Service";
static constexpr const char *OB_SERVICE_DESCRIPTION  = "SeekDB (OceanBase) distributed relational database service";

int ob_install_win_service(const char *service_name, int argc, char *argv[]);
int ob_remove_win_service(const char *service_name);

typedef int (*ObServiceMainFunc)(int argc, char *argv[]);
int ob_start_as_win_service(const char *service_name, ObServiceMainFunc main_func,
                            int argc, char *argv[]);

void ob_report_win_service_running();
void ob_report_win_service_stopped(DWORD exit_code);

} // namespace observer
} // namespace oceanbase

#endif // _WIN32

#endif // OCEANBASE_OBSERVER_OB_WIN_SERVICE_H_
