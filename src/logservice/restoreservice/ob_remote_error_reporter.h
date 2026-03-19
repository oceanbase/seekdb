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
#ifndef OCEANBASE_LOGSERVICE_OB_REMOTE_ERROR_REPORTER_H_
#define OCEANBASE_LOGSERVICE_OB_REMOTE_ERROR_REPORTER_H_

#include <cstdint>
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_define.h"
namespace oceanbase
{
namespace storage
{
class ObLSService;
class ObLS;
}
namespace logservice
{
class ObLogService;
class ObRemoteErrorReporter
{
public:
  ObRemoteErrorReporter();
  virtual ~ObRemoteErrorReporter();
  int init(const uint64_t tenant_id, storage::ObLSService *ls_svr);
  void destroy();
  int report_error();

private:
  int do_report_(storage::ObLS &ls);
  int report_standby_error_(storage::ObLS &ls, share::ObTaskId &trace_id, const int ret_code);
private:
  static const int64_t CHECK_ERROR_INTERVAL = 5 * 1000 * 1000L;
private:
  bool inited_;
  uint64_t tenant_id_;
  int64_t last_check_ts_;
  storage::ObLSService *ls_svr_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObRemoteErrorReporter);
};
} // namespace logservice
} // namespace oceanbase

#endif
