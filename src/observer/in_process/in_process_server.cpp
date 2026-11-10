// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "in_process_server.h"
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>
#include <curl/curl.h>
#include "lib/oblog/ob_log.h"
#include "lib/thread/protected_stack_allocator.h"
#include "lib/worker.h"
#include "observer/ob_server.h"
#include "observer/ob_server_options.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::observer;

namespace {
__attribute__((used)) const char artifact_metadata[] =
    "SEEKDB_IN_PROCESS_ARTIFACT_BUILD_ID=" SEEKDB_IN_PROCESS_BUILD_ID;
} // namespace

namespace oceanbase {
namespace in_process {
int32_t run_server(rust::Str directory, rust::Slice<const rust::String> parameters,
                   rust::Fn<void()> report_running)
{
  int ret = OB_SUCCESS;
  static std::vector<std::string> storage;
  for (const rust::String &parameter : parameters) {
    storage.emplace_back(parameter);
  }
  ObServerOptions options;
  options.in_process_ = true;
  options.nodaemon_ = true;
  ret = options.base_dir_.assign(std::string(directory).c_str());
  for (size_t i = 0; OB_SUCC(ret) && i + 1 < storage.size(); i += 2) {
    ret = options.parameters_.push_back(std::make_pair(ObString(storage[i].c_str()), ObString(storage[i + 1].c_str())));
  }
  if (OB_FAIL(ret)) {
  } else if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    lib::ObStackHeaderGuard stack_header_guard;
    lib::Worker worker;
    lib::Worker::set_worker_to_thread_local(&worker);
    OB_LOGGER.set_log_level(DEFAULT_LOG_LEVEL);
    OB_LOGGER.set_file_name("log/seekdb.log", true, false);
    ObServer &server = ObServer::get_instance();
    if (OB_FAIL(server.init(options))) {
    } else if (OB_FAIL(server.start())) {
    } else {
      report_running();
      while (true) {
        pause();
      }
    }
    lib::Worker::set_worker_to_thread_local(nullptr);
  }
  return ret;
}
} // namespace in_process
} // namespace oceanbase
