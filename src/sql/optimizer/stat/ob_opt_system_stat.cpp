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

#define USING_LOG_PREFIX SQL_OPT
#include "data_plane/ob_i_optimizer_storage_service.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/optimizer/stat/ob_opt_system_stat.h"


namespace oceanbase {
namespace common {
using namespace sql;

OB_DEF_SERIALIZE(ObOptSystemStat) {
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              last_analyzed_,
              cpu_speed_,
              disk_seq_read_speed_,
              disk_rnd_read_speed_,
              network_speed_
              );
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObOptSystemStat) {
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              last_analyzed_,
              cpu_speed_,
              disk_seq_read_speed_,
              disk_rnd_read_speed_,
              network_speed_
              );
  return len;
}

OB_DEF_DESERIALIZE(ObOptSystemStat) {
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              last_analyzed_,
              cpu_speed_,
              disk_seq_read_speed_,
              disk_rnd_read_speed_,
              network_speed_
              );
  return ret;
}


OptSystemIoBenchmark& OptSystemIoBenchmark::get_instance()
{
  static OptSystemIoBenchmark benchmark;
  return benchmark;
}

int OptSystemIoBenchmark::run_benchmark(ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  data_plane::ObIOptimizerStorageService *storage_service =
      ::oceanbase::share::server_service<::oceanbase::data_plane::ObIOptimizerStorageService>();
  if (OB_ISNULL(storage_service)) {
    ret = OB_NOT_INIT;
    LOG_WARN("optimizer storage service is not available", K(ret));
  } else if (OB_FAIL(storage_service->run_io_benchmark(
                 allocator, disk_rnd_read_speed_, disk_seq_read_speed_))) {
    LOG_WARN("failed to run storage IO benchmark", K(ret));
  } else {
    init_ = true;
  }
  return ret;
}

//TODO: collect system stat with workload

}
}
