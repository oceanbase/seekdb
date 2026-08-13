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

#define USING_LOG_PREFIX SERVER

#include "standby/ob_standby_source_util.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase
{
namespace standby
{

int StandbySourceParser::get_first_service_addr(
    const common::ObString &log_restore_source,
    common::ObAddr &addr)
{
  int ret = OB_SUCCESS;
  addr.reset();
  common::ObString source = log_restore_source.trim();
  common::ObString addr_str;
  const common::ObString service_prefix = common::ObString::make_string("SERVICE=");

  if (source.empty()) {
    ret = OB_ENTRY_NOT_EXIST;
  } else if (source.prefix_match_ci(service_prefix)) {
    source = common::ObString(source.length() - service_prefix.length(),
        source.ptr() + service_prefix.length()).trim();
    int64_t token_len = 0;
    while (token_len < source.length()
        && !isspace(static_cast<unsigned char>(source.ptr()[token_len]))) {
      ++token_len;
    }
    addr_str.assign_ptr(source.ptr(), static_cast<int32_t>(token_len));
    const char *semicolon = addr_str.find(';');
    if (nullptr != semicolon) {
      addr_str.clip(semicolon);
    }
    addr_str = addr_str.trim();
    if (addr_str.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("service standby source has no address", KR(ret));
    }
  } else {
    bool is_plain_addr = true;
    for (int64_t i = 0; is_plain_addr && i < source.length(); ++i) {
      const char ch = source.ptr()[i];
      is_plain_addr = ('=' != ch && ';' != ch
          && !isspace(static_cast<unsigned char>(ch)));
    }
    if (!is_plain_addr) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("only SERVICE or a plain address is supported for standby source", KR(ret));
    } else {
      addr_str = source;
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(addr.parse_from_string(addr_str))) {
    } else if (!addr.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid standby service address", KR(ret), K(addr_str));
    }
  }
  return ret;
}

} // namespace standby
} // namespace oceanbase
