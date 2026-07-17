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

#ifndef OB_COMMON_OB_TRACE_ID_ADAPTOR_H_
#define OB_COMMON_OB_TRACE_ID_ADAPTOR_H_

#include <stdint.h>
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace common
{

class ObTraceIdAdaptor
{
  OB_UNIS_VERSION(1);

public:
  ObTraceIdAdaptor() { reset(); }
  void reset()
  {
    uval_[0] = 0;
    uval_[1] = 0;
    uval_[2] = 0;
    uval_[3] = 0;
  }
  const uint64_t* get() const { return uval_; }
  void set(const uint64_t *uval) { uval_[0] = uval[0]; uval_[1] = uval[1]; uval_[2] = uval[2]; uval_[3] = uval[3]; }
  int64_t to_string(char *buf, const int64_t buf_len) const;
  bool operator==(const ObTraceIdAdaptor &other) const
  {
    return (uval_[0] == other.uval_[0]) && (uval_[1] == other.uval_[1]) && 
           (uval_[2] == other.uval_[2]) && (uval_[3] == other.uval_[3]);
  }
  ObTraceIdAdaptor &operator=(const ObTraceIdAdaptor &other)
  {
    uval_[0] = other.uval_[0];
    uval_[1] = other.uval_[1];
    uval_[2] = other.uval_[2];
    uval_[3] = other.uval_[3];
    return *this;
  }
private:
  uint64_t uval_[4];
};

}// namespace common
}// namespace oceanbase

#endif //OB_COMMON_OB_TRACE_ID_ADAPTOR_H_

