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

#include "observer/ob_server_struct.h"
#include "ob_all_virtual_dump_info.h"
#include "observer/omt/ob_server_runtime.h"
#include "observer/omt/ob_server_runtime_controller.h"

namespace oceanbase
{
using namespace lib;
namespace observer
{
ObAllVirtualDumpInfo::ObAllVirtualDumpInfo()
  : is_inited_(false)
{
}

ObAllVirtualDumpInfo::~ObAllVirtualDumpInfo()
{
}

int ObAllVirtualDumpInfo::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    auto func = [&] (omt::ObServerRuntime &t) {
      int ret = OB_SUCCESS;
      const int64_t col_count = output_column_ids_.count();
      ObObj *cells = cur_row_.cells_;
      for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
        uint64_t col_id = output_column_ids_.at(i);
        switch (col_id) {
        case MIN_CPU:
          cells[i].set_double(t.min_cpu());
          break;
        case MAX_CPU:
          cells[i].set_double(t.max_cpu());
          break;
        case STOPPED:
          cells[i].set_int(t.stopped_);
          break;
        case RECV_MYSQL_COUNT:
          cells[i].set_int(t.recv_mysql_cnt_);
          break;
        case RECV_TASK_COUNT:
          cells[i].set_int(t.recv_task_cnt_);
          break;
        case WORKER_COUNT:
          cells[i].set_int(t.workers_.get_size());
          break;
        case REQUEST_QUEUE_SIZE:
          cells[i].set_int(t.req_queue_.size());
          break;
        case QUEUE_0_SIZE:
          cells[i].set_int(t.req_queue_.queue_size(0));
          break;
        case QUEUE_1_SIZE:
          cells[i].set_int(t.req_queue_.queue_size(1));
          break;
        case QUEUE_2_SIZE:
          cells[i].set_int(t.req_queue_.queue_size(2));
          break;
        case QUEUE_3_SIZE:
          cells[i].set_int(t.req_queue_.queue_size(3));
          break;
        case QUEUE_4_SIZE:
          cells[i].set_int(t.req_queue_.queue_size(4));
          break;
        case QUEUE_5_SIZE:
          cells[i].set_int(t.req_queue_.queue_size(5));
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id, ", K(ret), K(col_id));
        }
      }
      if (OB_SUCC(ret)) {
        // The scanner supports up to 64M, so the overflow situation is not considered for the time being
        if (OB_FAIL(scanner_.add_row(cur_row_))) {
          SERVER_LOG(WARN, "fail to add row", K(ret), K(cur_row_));
        }
      }
      return ret;
    };

    omt::ObServerRuntimeController *controller = GCTX.server_runtime_controller_;
    if (OB_ISNULL(controller)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "runtime controller is null", K(ret));
    } else {
      omt::ObServerRuntime *runtime = nullptr;
      if (OB_FAIL(controller->get_runtime(runtime))) {
        SERVER_LOG(WARN, "get runtime failed", K(ret));
      } else if (OB_ISNULL(runtime)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "runtime is null", K(ret));
      } else if (OB_FAIL(func(*runtime))) {
        SERVER_LOG(WARN, "collect runtime info failed", K(ret));
      }
      if (OB_SUCC(ret)) {
        scanner_it_ = scanner_.begin();
        is_inited_ = true;
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
      if (OB_ITER_END != ret) {
        SERVER_LOG(WARN, "fail to get next row", K(ret));
      }
    } else {
      row = &cur_row_;
    }
  }

  return ret;
}

} /* namespace observer */
} /* namespace oceanbase */
