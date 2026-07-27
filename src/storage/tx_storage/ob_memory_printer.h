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

#ifndef OCEABASE_STORAGE_MEMORY_PRINTER_
#define OCEABASE_STORAGE_MEMORY_PRINTER_

#include "lib/task/ob_timer.h"          // ObTimerTask
#include "lib/lock/ob_mutex.h"          // ObMutex

namespace oceanbase
{
namespace storage
{

// Periodically prints process memory and memstore usage.
class ObPrintMemoryUsage : public ObTimerTask
{
public:
  ObPrintMemoryUsage() {}
  virtual ~ObPrintMemoryUsage() {}
public:
  virtual void runTimerTask();
private:
  DISALLOW_COPY_AND_ASSIGN(ObPrintMemoryUsage);
};

class ObMemoryPrinter
{
public:
  static ObMemoryPrinter &get_instance();
  // Register the memory printer with a timer thread.
  int register_timer_task(common::ObTimer &timer);
  // Print process memory and memstore usage.
  int print_memory_usage();
private:
  ObMemoryPrinter() : print_mutex_(common::ObLatchIds::MEMORY_USAGE_LOCK) {}
  virtual ~ObMemoryPrinter() {}
  int print_memory_usage_(char *print_buf,
                          int64_t buf_len,
                          int64_t &pos);
private:
  // the timer will register to a print thread.
  ObPrintMemoryUsage print_task_;
  // the mutex is used to make sure not print concurrently.
  lib::ObMutex print_mutex_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMemoryPrinter);
};

} // storage
} // oceanbase
#endif
