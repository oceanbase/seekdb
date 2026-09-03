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

#include "lib/alloc/alloc_struct.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase
{

using namespace common;

namespace lib
{
ObMallocHookAttrGuard::ObMallocHookAttrGuard(const ObMemAttr& attr)
  : old_attr_(get_tl_mem_attr())
{
  get_tl_mem_attr() = attr;
  get_tl_mem_attr().ctx_id_ = ObCtxIds::GLIBC;
}

ObMallocHookAttrGuard::~ObMallocHookAttrGuard()
{
  get_tl_mem_attr() = old_attr_;
}

bool ObLabel::operator==(const ObLabel &other) const
{
  bool bret = false;
  if (is_valid() && other.is_valid()) {
    if (str_[0] == other.str_[0]) {
      if (0 == STRCMP(str_, other.str_)) {
        bret = true;
      }
    }
  } else if (!is_valid() && !other.is_valid()) {
    bret = true;
  }
  return bret;
}

ObLabel::operator const char *() const
{
  return str_;
}

int64_t ObLabel::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  (void)common::logdata_printf(
      buf, buf_len, pos, "%s", (const char*)(*this));
  return pos;
}

int64_t ObMemAttr::to_string(char* buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  (void)common::logdata_printf(
      buf, buf_len, pos,
      "label=%s, ctx_id=%ld, prio=%d",
      (const char *)label_, ctx_id_, prio_);
  return pos;
}

} // end of namespace lib
} // end of namespace oceanbase
