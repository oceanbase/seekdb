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

#include "common/ob_version_def.h"

namespace oceanbase 
{
namespace common 
{

int64_t VersionUtil::print_version_str(char *buf, const int64_t buf_len, uint64_t version) 
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  const uint32_t major = OB_VSN_MAJOR(version);
  const uint16_t minor = OB_VSN_MINOR(version);
  const uint8_t major_patch = OB_VSN_MAJOR_PATCH(version);
  const uint8_t minor_patch = OB_VSN_MINOR_PATCH(version);
  if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%u.%u.%u.%u",
              major, minor, major_patch, minor_patch))) {
    COMMON_LOG(WARN, "fail to print version str", K(ret), K(version));
  }
  if (OB_FAIL(ret)) {
    pos = OB_INVALID_INDEX;
  }
  return pos;
}

ObVersionPrinter::ObVersionPrinter(const uint64_t version)
    : version_val_(version), version_str_{0}
{
  if (OB_INVALID_INDEX ==
      VersionUtil::print_version_str(version_str_, OB_SERVER_VERSION_LENGTH, version)) {
    MEMSET(version_str_, 0, OB_SERVER_VERSION_LENGTH);
  }
}

} // namespace common
} // namespace oceanbase
