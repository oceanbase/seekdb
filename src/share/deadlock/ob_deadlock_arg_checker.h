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

#ifndef OCEANBASE_SHARE_DEADLOCK_OB_DEADLOCK_ARG_CHECKER_H
#define OCEANBASE_SHARE_DEADLOCK_OB_DEADLOCK_ARG_CHECKER_H

#include "lib/container/ob_array.h"
#include "lib/oblog/ob_log_module.h"
#include "ob_deadlock_parameters.h"
#include "lib/utility/utility.h"
#include <stdint.h>

namespace oceanbase
{
namespace share
{
namespace detector
{

using namespace common;

template <class T>
inline bool check_args(const T &arg)
{
  return arg.is_valid();
}

template <class T>
inline bool check_args(T *arg)
{
  return nullptr != arg;
}

template <class T>
inline bool check_args(const common::ObIArray<T> &arg)
{
  return !arg.empty();
}

template <>
inline bool check_args<int64_t>(const int64_t &arg)
{
  return INVALID_VALUE != arg;
}

template <>
inline bool check_args<uint64_t>(const uint64_t &arg)
{
  return INVALID_VALUE != arg;
}

template <class HEAD, class ...Args>
inline int check_args(const HEAD &head, const Args &...rest)
{
  int ret = OB_SUCCESS;
  if (!check_args(head)) {
    ret = OB_INVALID_ARGUMENT;
    DETECT_LOG(WARN, "arg invalid");
  } else {
    ret = check_args(rest...);
  }
  return ret;
}

#define CHECK_ARGS(args...) \
do {\
  int temp_ret = check_args(args);\
  if (temp_ret == OB_INVALID_ARGUMENT) {\
    return temp_ret;\
  }\
} while(0)

#define CHECK_INIT()\
do {\
if (is_inited_ != true) {\
  DETECT_LOG_RET(WARN, OB_NOT_INIT, "not init yet", K_(is_inited));\
  return OB_NOT_INIT;\
}} while(0)

#define CHECK_START()\
do {\
if (is_running_ != true) {\
  DETECT_LOG_RET(WARN, OB_NOT_RUNNING, "not running", K_(is_running));\
  return OB_NOT_RUNNING;\
}} while(0)

#define CHECK_ENABLED()\
do {\
if (!is_deadlock_enabled()) {\
  DETECT_LOG_RET(WARN, OB_NOT_RUNNING, "deadlock not enabled", K(is_deadlock_enabled()));\
  return OB_NOT_RUNNING;\
}} while(0)

#define CHECK_INIT_AND_START() CHECK_INIT();CHECK_START()

}
}
}
#endif
