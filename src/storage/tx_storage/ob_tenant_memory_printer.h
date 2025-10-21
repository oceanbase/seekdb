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

#ifndef OCEABASE_STORAGE_TENANT_MEMSTORE_PRINTER_
#define OCEABASE_STORAGE_TENANT_MEMSTORE_PRINTER_

#include "lib/task/ob_timer.h"          // ObTimerTask
#include "lib/lock/ob_mutex.h"          // ObMutex

namespace oceanbase
{
namespace storage
{

// this class is a timer that will call ObTenantMemoryPrinter to
// print the tenant memstore usage info.
class ObPrintTenantMemoryUsage : public ObTimerTask
{
public:
  ObPrintTenantMemoryUsage() {}
  virtual ~ObPrintTenantMemoryUsage() {}
public:
  virtual void runTimerTask();
private:
  DISALLOW_COPY_AND_ASSIGN(ObPrintTenantMemoryUsage);
};

class ObTenantMemoryPrinter
{
public:
  static ObTenantMemoryPrinter &get_instance();
  // register memstore printer to a timer thread,
  // which thread is used to print the tenant memstore usage.
  // @param[in] tg_id, the thread tg id.
  int register_timer_task(int tg_id);
  // print all the tenant memstore usage.
  int print_tenant_usage();
private:
  ObTenantMemoryPrinter() : print_mutex_(common::ObLatchIds::TENANT_MEM_USAGE_LOCK) {}
  virtual ~ObTenantMemoryPrinter() {}
  int print_tenant_usage_(const uint64_t tenant_id,
                          char *print_buf,
                          int64_t buf_len,
                          int64_t &pos);
private:
  // the timer will register to a print thread.
  ObPrintTenantMemoryUsage print_task_;
  // the mutex is used to make sure not print concurrently.
  lib::ObMutex print_mutex_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTenantMemoryPrinter);
};

} // storage
} // oceanbase
#endif
