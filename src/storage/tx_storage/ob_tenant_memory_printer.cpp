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

#include "storage/tx_storage/ob_tenant_freezer.h"          // ObTenantFreezer
#include "share/rc/ob_module_provider.h"
#include "storage/tx_storage/ob_tenant_memory_printer.h"

namespace oceanbase
{
using namespace share;
namespace storage
{
void ObPrintTenantMemoryUsage::runTimerTask()
{
  GMEMCONF.check_limit(GCONF._ignore_system_memory_over_limit_error);
  LOG_INFO("=== Run print tenant memory usage task ===");
  ObTenantMemoryPrinter &printer = ObTenantMemoryPrinter::get_instance();
  printer.print_tenant_usage();
}

ObTenantMemoryPrinter &ObTenantMemoryPrinter::get_instance()
{
  static ObTenantMemoryPrinter instance_;
  return instance_;
}

int ObTenantMemoryPrinter::register_timer_task(int tg_id)
{
  int ret = OB_SUCCESS;
  const bool is_repeated = true;
  const int64_t print_delay = 10 * 1000000; // 10s
  if (OB_FAIL(TG_SCHEDULE(tg_id,
                          print_task_,
                          print_delay,
                          is_repeated))) {
  }
  return ret;
}

int ObTenantMemoryPrinter::print_tenant_usage()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  static const int64_t BUF_LEN = 4LL << 10;
  char print_buf[BUF_LEN] = "";
  int64_t pos = 0;
  omt::ObMultiTenant *omt = GCTX.omt_;
  if (OB_FAIL(print_mutex_.trylock())) {
    // Guaranteed serial printing
    // do-nothing
  } else {
    if (OB_FAIL(databuff_printf(print_buf, BUF_LEN, pos,
                                "=== TENANTS MEMORY INFO ===\n"
                                "unmanaged_memory_size=% '15ld\n",
                                lib::get_unmanaged_memory_size()))) {
    } else if (OB_ISNULL(omt)) {
      // do nothing
    } else {
      if (OB_SUCCESS != (tmp_ret = print_tenant_usage_(print_buf,
                                                       BUF_LEN,
                                                       pos))) {
      }
    }

    if (OB_SIZE_OVERFLOW == ret) {
      // If the buffer is not enough, truncate directly
      ret = OB_SUCCESS;
      print_buf[BUF_LEN - 2] = '\n';
      print_buf[BUF_LEN - 1] = '\0';
    }
    if (OB_SUCCESS == ret) {
      _STORAGE_LOG(INFO, "====== tenants memory info ======\n%s", print_buf);
    }

    print_mutex_.unlock();
  }
  return ret;
}

int ObTenantMemoryPrinter::print_tenant_usage_(
    char *print_buf,
    int64_t buf_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  MOD_SCOPE {
    storage::ObTenantFreezer *freezer = nullptr;
    if (FALSE_IT(freezer = share::g_mp->tenant_freezer())) {
    } else if (OB_ISNULL(freezer)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("freezer is null", K(ret));
    } else if (OB_FAIL(freezer->print_tenant_usage(print_buf,
                                                   buf_len,
                                                   pos))) {
    } else {
      // do nothing
    }
  }
  return ret;
}

} // storage
} // oceanbase
