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

#define USING_LOG_PREFIX STORAGE

#include "share/ob_force_print_log.h"
#include "share/ob_plugin_helper.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace share
{

const char *OB_PLUGIN_PREFIX = "ob_builtin_";
const char *OB_PLUGIN_SUFFIX = "_plugin";
const char *OB_PLUGIN_VERSION_SUFFIX = "_plugin_version";
const char *OB_PLUGIN_SIZE_SUFFIX = "_sizeof_plugin";

#define OB_PLUGIN_GETTER(buf, buf_len, name, name_len, suffix, suffix_len)              \
do {                                                                                    \
  const int64_t prefix_len = STRLEN(OB_PLUGIN_PREFIX);                                  \
  if (OB_UNLIKELY(buf_len <= prefix_len + name_len + suffix_len)) {                     \
    ret = OB_INVALID_ARGUMENT;                                                          \
    LOG_WARN("This buffer is too small to accommodate all of name", K(ret), K(buf_len), \
        K(prefix_len), K(name_len), K(suffix_len));                                     \
  } else {                                                                              \
    MEMCPY(buf + prefix_len + name_len, suffix, suffix_len);                            \
    MEMCPY(buf + prefix_len, name, name_len);                                           \
    MEMCPY(buf, OB_PLUGIN_PREFIX, prefix_len);                                          \
    buf[prefix_len + name_len + suffix_len] = '\0';                                     \
  }                                                                                     \
} while (false)




int ObPluginName::set_name(const char *name)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("The name is nullptr", K(ret), KP(name));
  } else if (OB_UNLIKELY(STRLEN(name) >= OB_PLUGIN_NAME_LENGTH)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("The name is too long", K(ret), KCSTRING(name));
  } else {
    int i = 0;
    while ('\0' != name[i]) {
      name_[i] = tolower(name[i]);
      ++i;
    }
    name_[i] = '\0';
  }
  return ret;
}

// Some input has no '\0',
int ObPluginName::set_name(const ObString &name)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(name.empty() || (name.length() >= OB_PLUGIN_NAME_LENGTH))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Parser name invalid", K(ret), K(name));
  } else {
    ObString::obstr_size_t i = 0;
    for (; (i < name.length()) && ('\0' != name[i]); ++i) {
      name_[i] = tolower(name[i]);
    }
    name_[i] = '\0';
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
