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

#ifndef SRC_LIB_UTILITY_OB_HANG_FATAL_ERROR_H_
#define SRC_LIB_UTILITY_OB_HANG_FATAL_ERROR_H_

#include <exception>
#include "lib/coro/co_var.h"

namespace oceanbase
{
namespace common
{
extern void right_to_die_or_duty_to_live();
int64_t get_fatal_error_thread_id();
void set_fatal_error_thread_id(int64_t thread_id);


RLOCAL_EXTERN(bool, in_try_stmt);

struct OB_BASE_EXCEPTION : public std::exception
{
  virtual const char *what() const throw() override { return nullptr; }
  virtual int get_errno() { return 0; }
};

template <int ERRNO>
struct OB_EXCEPTION : public OB_BASE_EXCEPTION
{
  virtual int get_errno() { return ERRNO; }
};

}
}

#endif /* SRC_LIB_UTILITY_OB_HANG_FATAL_ERROR_H_ */
