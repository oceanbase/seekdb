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

#include "ob_version_parser.h"
#include "lib/ob_define.h"
namespace oceanbase
{
namespace common
{
static int parse_version(const char *str, uint64_t *versions, const int64_t size)
{
  int ret = OB_SUCCESS;
  int64_t i = 0;
  char buf[64] = {0};
  char *ptr = buf;
  const char *delim = ".";
  char *saveptr = NULL;
  char *token = NULL;
  const int64_t VERSION_ITEM = 4;
  const int64_t LAST_VERSION_ITEM = VERSION_ITEM - 1;

  if (NULL == str || NULL == versions || VERSION_ITEM > size) {
    COMMON_LOG(WARN, "invalid argument", KP(str), KP(versions), K(size));
    ret = OB_INVALID_ARGUMENT;
  } else if (strlen(str) >= sizeof(buf)) {
    COMMON_LOG(WARN, "invalid version", "version", str);
    ret = OB_INVALID_ARGUMENT;
  } else {
    strncpy(buf, str, sizeof(buf) - 1);
    for (i = 0; i < size; i++) {
      if (NULL != (token = strtok_r(ptr, delim, &saveptr))) {
        versions[i] = atoi(token);
      } else {
        break;
      }
      ptr = NULL;
    }
    if (VERSION_ITEM < i || LAST_VERSION_ITEM > i) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "invalid package version", KR(ret), "version", str, K(i), K(VERSION_ITEM));
    } else if (i == LAST_VERSION_ITEM) {
      // Pad a three-part version with a trailing zero.
      versions[VERSION_ITEM - 1] = 0;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(
        versions[ObVersionParser::MAJOR_POS] > OB_VSN_MAJOR_MASK
        || versions[ObVersionParser::MINOR_POS] > OB_VSN_MINOR_MASK
        || versions[ObVersionParser::MAJOR_PATCH_POS] > OB_VSN_MAJOR_PATCH_MASK
        || versions[ObVersionParser::MINOR_PATCH_POS] > OB_VSN_MINOR_PATCH_MASK)) {
      ret = OB_SIZE_OVERFLOW;
      COMMON_LOG(WARN, "invalid package version",
                 KR(ret), "version", str,
                 "major", versions[ObVersionParser::MAJOR_POS],
                 "minor", versions[ObVersionParser::MINOR_POS],
                 "major_patch", versions[ObVersionParser::MAJOR_PATCH_POS],
                 "minor_patch", versions[ObVersionParser::MINOR_PATCH_POS]);
    }
  }
  return ret;
}

int64_t ObVersionParser::print_vsn(char *buf, const int64_t buf_len, uint64_t version)
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
  if (OB_FAIL(ret)) {
    pos = OB_INVALID_INDEX;
  }
  return pos;
}

int64_t ObVersionParser::print_version_str(char *buf, const int64_t buf_len, uint64_t version)
{
  return VersionUtil::print_version_str(buf, buf_len, version);
}

int ObVersionParser::is_valid(const char *verstr)
{
  int ret = OB_SUCCESS;
  uint64_t items[MAX_VERSION_ITEM] = {0};
  if (NULL == verstr) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(parse_version(verstr, items, MAX_VERSION_ITEM))) {
  }
  return ret;
}

int ObVersionParser::get_version(const common::ObString &verstr, uint64_t &version)
{
  int ret = OB_SUCCESS;
  char buf[OB_VERSION_LENGTH];
  version = 0;

  if (OB_FAIL(databuff_printf(buf, OB_VERSION_LENGTH, "%.*s", verstr.length(), verstr.ptr()))) {
  } else if (OB_FAIL(get_version(buf, version))) {
  }

  return ret;
}

int ObVersionParser::get_version(const char *verstr, uint64_t &version)
{
  int ret = OB_SUCCESS;
  uint64_t items[MAX_VERSION_ITEM] = {0};
  if (NULL == verstr) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(parse_version(verstr, items, MAX_VERSION_ITEM))) {
  } else {
    version = cal_version(items[ObVersionParser::MAJOR_POS],
                          items[ObVersionParser::MINOR_POS],
                          items[ObVersionParser::MAJOR_PATCH_POS],
                          items[ObVersionParser::MINOR_PATCH_POS]);
  }
  return ret;
}

} // end namespace common
} // end namespace oceanbase
