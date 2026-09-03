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

#define USING_LOG_PREFIX STORAGE

#include "storage/tx_storage/ob_memstore_freezer.h"          // ObMemstoreFreezer
#include "share/rc/ob_server_runtime.h"
#include "storage/api/storage/runtime/ob_i_server_runtime.h"
#include "storage/tx_storage/ob_memory_printer.h"

namespace oceanbase
{
using namespace share;
namespace storage
{
void ObPrintMemoryUsage::runTimerTask()
{
  LOG_INFO("run memory usage task");
  ObMemoryPrinter &printer = ObMemoryPrinter::get_instance();
  printer.print_memory_usage();
}

ObMemoryPrinter &ObMemoryPrinter::get_instance()
{
  static ObMemoryPrinter instance_;
  return instance_;
}

int ObMemoryPrinter::register_timer_task(common::ObTimer &timer)
{
  int ret = OB_SUCCESS;
  const bool is_repeated = true;
  const int64_t print_delay = 10 * 1000000; // 10s
  if (OB_FAIL(timer.schedule(print_task_, print_delay, is_repeated))) {
  }
  return ret;
}

int ObMemoryPrinter::print_memory_usage()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  static const int64_t BUF_LEN = 4LL << 10;
  char print_buf[BUF_LEN] = "";
  int64_t pos = 0;
  if (OB_ISNULL(
          ::oceanbase::share::server_service<::oceanbase::storage::ObIServerRuntime>())) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(print_mutex_.trylock())) {
    ret = OB_SUCCESS;
  } else {
    if (OB_FAIL(databuff_printf(print_buf, BUF_LEN, pos,
                                "=== MEMORY INFO ===\n"))) {
    } else if (OB_SUCCESS != (tmp_ret = print_memory_usage_(print_buf,
                                                            BUF_LEN,
                                                            pos))) {
    }

    if (OB_SIZE_OVERFLOW == ret) {
      // If the buffer is not enough, truncate directly
      ret = OB_SUCCESS;
      print_buf[BUF_LEN - 2] = '\n';
      print_buf[BUF_LEN - 1] = '\0';
    }
    if (OB_SUCCESS == ret) {
      _STORAGE_LOG(INFO, "====== memory info ======\n%s", print_buf);
    }

    print_mutex_.unlock();
  }
  return ret;
}

int ObMemoryPrinter::print_memory_usage_(
    char *print_buf,
    int64_t buf_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  SERVER_MODULE_SCOPE {
    storage::ObMemstoreFreezer *freezer = ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>();
    if (OB_ISNULL(freezer)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("freezer is null", K(ret));
    } else if (OB_FAIL(freezer->print_memory_usage(print_buf,
                                                   buf_len,
                                                   pos))) {
    }
  }
  return ret;
}

} // storage
} // oceanbase
