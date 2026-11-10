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

#include "share/ob_server_struct.h"
#include "share/rc/ob_server_runtime.h"
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
      DatumValue *cells = cur_row_.cells_;
      for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
        uint64_t col_id = output_column_ids_.at(i);
        switch (col_id) {
        case MIN_CPU:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_double(cells, cur_row_.count_, i, t.min_cpu()) : ret);
          break;
        case MAX_CPU:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_double(cells, cur_row_.count_, i, t.max_cpu()) : ret);
          break;
        case STOPPED:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, t.stopped_) : ret);
          break;
        case RECV_MYSQL_COUNT:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, t.recv_mysql_cnt_) : ret);
          break;
        case RECV_TASK_COUNT:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, t.recv_task_cnt_) : ret);
          break;
        case WORKER_COUNT:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, t.workers_.get_size()) : ret);
          break;
        case REQUEST_QUEUE_SIZE:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, t.req_queue_.size()) : ret);
          break;
        case QUEUE_0_SIZE:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, t.req_queue_.queue_size(0)) : ret);
          break;
        case QUEUE_1_SIZE:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, t.req_queue_.queue_size(1)) : ret);
          break;
        case QUEUE_2_SIZE:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, t.req_queue_.queue_size(2)) : ret);
          break;
        case QUEUE_3_SIZE:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, t.req_queue_.queue_size(3)) : ret);
          break;
        case QUEUE_4_SIZE:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, t.req_queue_.queue_size(4)) : ret);
          break;
        case QUEUE_5_SIZE:
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, t.req_queue_.queue_size(5)) : ret);
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id, ", K(ret), K(col_id));
        }
      }
      if (OB_SUCC(ret)) {
        // The scanner supports up to 64M, so the overflow situation is not considered for the time being
        if (OB_FAIL(scanner_.add_row(cur_row_))) {
        }
      }
      return ret;
    };

    omt::ObServerRuntimeController *controller = ::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>();
    if (OB_ISNULL(controller)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "runtime controller is null", K(ret));
    } else {
      omt::ObServerRuntime *runtime = nullptr;
      if (OB_FAIL(controller->get_runtime(runtime))) {
      } else if (OB_ISNULL(runtime)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "runtime is null", K(ret));
      } else if (OB_FAIL(func(*runtime))) {
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
