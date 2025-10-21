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

#ifndef OCEANBASE_SIGNAL_STRUCT_H_
#define OCEANBASE_SIGNAL_STRUCT_H_

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace common
{

extern void ob_signal_handler(int, siginfo_t *, void *);
typedef void (*signal_handler_t)(int, siginfo_t *, void *);
extern signal_handler_t &get_signal_handler();
extern bool g_redirect_handler;
extern const int SIG_STACK_SIZE;
extern uint64_t g_rlimit_core;

struct ObSignalHandlerGuard
{
public:
  ObSignalHandlerGuard(signal_handler_t handler)
    : last_(get_signal_handler())
  {
    get_signal_handler() = handler;
  }
  ~ObSignalHandlerGuard()
  {
    get_signal_handler() = last_;
  }
private:
  const signal_handler_t last_;
};

extern int install_ob_signal_handler();

struct ObSqlInfo
{
  ObString sql_string_;
  ObString sql_id_;
};

class ObSqlInfoGuard
{
public:
  ObSqlInfoGuard(const ObString &sql_string, const ObString &sql_id)
    : last_sql_info_(tl_sql_info)
	{
    tl_sql_info.sql_string_ = sql_string;
    tl_sql_info.sql_id_ = sql_id;
  }
	~ObSqlInfoGuard()
  {
    tl_sql_info = last_sql_info_;
  }
  static ObSqlInfo get_tl_sql_info()
  {
    return tl_sql_info;
  }
private:
  static thread_local ObSqlInfo tl_sql_info;
  ObSqlInfo last_sql_info_;
};

} // namespace common
} // namespace oceanbase

#define SQL_INFO_GUARD(sql_string, sql_id)                              \
oceanbase::common::ObSqlInfoGuard sql_info_guard(sql_string, sql_id);

#endif // OCEANBASE_SIGNAL_STRUCT_H_
