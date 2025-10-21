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

#ifndef _OB_RS_TEST_UTILS_H
#define _OB_RS_TEST_UTILS_H 1
#include <gtest/gtest.h>
#include "lib/utility/ob_test_util.h"
#include "rootserver/ob_server_manager.h"
#include "rootserver/ob_root_utils.h"
#include "lib/oblog/ob_log.h"
#include "lib/json/ob_json.h"
namespace oceanbase
{
namespace rootserver
{
class ObFakeCB : public ObIStatusChangeCallback
{
public:
  ObFakeCB() {}
  int wakeup_balancer() { return OB_SUCCESS; }
  int wakeup_daily_merger() { return OB_SUCCESS; }
  int on_start_server(const common::ObAddr &server) {UNUSED(server); return OB_SUCCESS;}
  int on_stop_server(const common::ObAddr &server) {UNUSED(server); return OB_SUCCESS;}
  int on_server_status_change(const common::ObAddr &server) {UNUSED(server); return OB_SUCCESS;}
};

class ObFakeServerChangeCB : public ObIServerChangeCallback
{
public:
  ObFakeServerChangeCB() {}
  virtual ~ObFakeServerChangeCB() {}
  virtual int on_server_change()
  {
    return OB_SUCCESS;
  }
};


class ObNeverStopForTestOnly : public share::ObCheckStopProvider
{
public:
  ObNeverStopForTestOnly() {}
  virtual ~ObNeverStopForTestOnly() {}
  virtual int check_stop() const { return OB_SUCCESS; }
};


// parse the case file using JSON parser
void ob_parse_case_file(common::ObArenaAllocator &allocator, const char* case_file, json::Value *&root);
// compare the result and output for the case
void ob_check_result(const char* base_dir, const char* casename);

} // end namespace rootserver
} // end namespace oceanbase

inline void init_oblog_for_rs_test(const char* test_name)
{
  char buf[256];
  snprintf(buf, 256, "rm -f %s.log*", test_name);
  system(buf);
  snprintf(buf, 256, "%s.log", test_name);
  OB_LOGGER.set_file_name(buf, true);
  OB_LOGGER.set_log_level("INFO");
  OB_LOGGER.set_mod_log_levels("ALL.*:INFO,COMMON.*:ERROR");
}
#endif /* _OB_RS_TEST_UTILS_H */
