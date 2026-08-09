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

#define USING_LOG_PREFIX COMMON

#include "common/ob_version_def.h"

#include <errno.h>
#include <stdlib.h>

#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace common
{

namespace
{
int parse_version(const char *str, uint64_t *versions, const int64_t size)
{
  int ret = OB_SUCCESS;
  char buf[64] = {0};
  char *saveptr = nullptr;
  char *token = nullptr;
  int64_t item_count = 0;
  static const int64_t VERSION_ITEM_COUNT = 4;
  static const int64_t MIN_VERSION_ITEM_COUNT = VERSION_ITEM_COUNT - 1;

  if (OB_ISNULL(str) || OB_ISNULL(versions) || size < VERSION_ITEM_COUNT) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid argument", K(ret), KP(str), KP(versions), K(size));
  } else if (strlen(str) >= sizeof(buf)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "version string is too long", K(ret), "version", str);
  } else {
    MEMCPY(buf, str, strlen(str));
    char *next = buf;
    while (item_count < size && nullptr != (token = strtok_r(next, ".", &saveptr))) {
      char *end = nullptr;
      errno = 0;
      const unsigned long long item = strtoull(token, &end, 10);
      if (ERANGE == errno || end == token || '\0' != *end) {
        ret = OB_INVALID_ARGUMENT;
        COMMON_LOG(WARN, "invalid version item", K(ret), "version", str, K(token));
        break;
      }
      versions[item_count++] = item;
      next = nullptr;
    }
    if (OB_SUCC(ret)
        && (item_count < MIN_VERSION_ITEM_COUNT || item_count > VERSION_ITEM_COUNT)) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "invalid version item count",
                 K(ret), "version", str, K(item_count), K(VERSION_ITEM_COUNT));
    } else if (OB_SUCC(ret) && MIN_VERSION_ITEM_COUNT == item_count) {
      versions[VERSION_ITEM_COUNT - 1] = 0;
    }
    if (OB_SUCC(ret)
        && OB_UNLIKELY(versions[VersionUtil::MAJOR_POS] > OB_VSN_MAJOR_MASK
                       || versions[VersionUtil::MINOR_POS] > OB_VSN_MINOR_MASK
                       || versions[VersionUtil::MAJOR_PATCH_POS] > OB_VSN_MAJOR_PATCH_MASK
                       || versions[VersionUtil::MINOR_PATCH_POS] > OB_VSN_MINOR_PATCH_MASK)) {
      ret = OB_SIZE_OVERFLOW;
      COMMON_LOG(WARN, "version item is too large",
                 K(ret), "version", str,
                 "major", versions[VersionUtil::MAJOR_POS],
                 "minor", versions[VersionUtil::MINOR_POS],
                 "major_patch", versions[VersionUtil::MAJOR_PATCH_POS],
                 "minor_patch", versions[VersionUtil::MINOR_PATCH_POS]);
    }
  }
  return ret;
}
} // namespace

int VersionUtil::is_valid(const char *verstr)
{
  uint64_t items[MAX_VERSION_ITEM] = {0};
  return parse_version(verstr, items, MAX_VERSION_ITEM);
}

int VersionUtil::get_version(const ObString &verstr, uint64_t &version)
{
  int ret = OB_SUCCESS;
  char buf[OB_SERVER_VERSION_LENGTH] = {0};
  version = 0;
  if (OB_FAIL(databuff_printf(
          buf, sizeof(buf), "%.*s", verstr.length(), verstr.ptr()))) {
  } else if (OB_FAIL(get_version(buf, version))) {
  }
  return ret;
}

int VersionUtil::get_version(const char *verstr, uint64_t &version)
{
  int ret = OB_SUCCESS;
  uint64_t items[MAX_VERSION_ITEM] = {0};
  version = 0;
  if (OB_FAIL(parse_version(verstr, items, MAX_VERSION_ITEM))) {
  } else {
    version = cal_version(items[MAJOR_POS],
                          items[MINOR_POS],
                          items[MAJOR_PATCH_POS],
                          items[MINOR_PATCH_POS]);
  }
  return ret;
}

int64_t VersionUtil::print_vsn(char *buf, const int64_t buf_len, uint64_t version)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  const uint32_t major = OB_VSN_MAJOR(version);
  const uint16_t minor = OB_VSN_MINOR(version);
  const uint8_t major_patch = OB_VSN_MAJOR_PATCH(version);
  const uint8_t minor_patch = OB_VSN_MINOR_PATCH(version);
  if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%lu(%u, %u, %u, %u)",
              version, major, minor, major_patch, minor_patch))) {
  }
  return OB_SUCC(ret) ? pos : OB_INVALID_INDEX;
}

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
