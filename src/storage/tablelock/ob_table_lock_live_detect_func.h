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
#ifndef OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_LIVE_DETECT_FUNC_H
#define OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_LIVE_DETECT_FUNC_H
#include <cstdint>
#include "deps/oblib/src/lib/ob_errno.h"
namespace oceanbase
{
namespace common
{
class ObStringHolder;
template <typename T>
class ObFunction;
}
namespace transaction
{
namespace tablelock
{
enum ObTableLockDetectType : uint8_t
{
  INVALID_DETECT_TYPE = 0,
  DETECT_SESSION_ALIVE = 1
};
template <typename... Args>
class ObTableLockDetectFunc
{
public:
  ObTableLockDetectFunc() : func_no_(INVALID_DETECT_TYPE), func_(nullptr){};
  ObTableLockDetectFunc(ObTableLockDetectType func_no, common::ObFunction<int(Args...)> func) :
    func_no_(func_no), func_(func){};

public:
  int call_function_by_param(const char *buf, bool &is_alive, Args &...args);
  int call_function_directly(Args &...args);
  int serialize_detect_func_param(char *buf, const Args &...args);
  int deserialize_detect_func_param(const char *buf, Args &...args);

private:
  template <typename T, typename... Others>
  int serialize_func_param_(char *buf, int64_t buf_len, int64_t &pos, const T &arg, const Others &...args);
  int serialize_func_param_(char *buf, int64_t buf_len, int64_t &pos) { return OB_SUCCESS; }
  template <typename T, typename... Others>
  int deserialize_func_param_(const char *buf, int64_t buf_len, int64_t &pos, T &arg, Others &...args);
  int deserialize_func_param_(const char *buf, int64_t buf_len, int64_t &pos) { return OB_SUCCESS; }

public:
  ObTableLockDetectType func_no_;
  common::ObFunction<int(Args...)> func_;
};

}  // namespace tablelock
}  // namespace transaction
}  // namespace oceanbase

#ifndef OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_LIVE_DETECT_FUNC_H_IPP
#  define OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_LIVE_DETECT_FUNC_H_IPP
#  include "ob_table_lock_live_detect_func.ipp"
#endif

#endif
