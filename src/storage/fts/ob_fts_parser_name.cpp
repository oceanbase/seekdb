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

#define USING_LOG_PREFIX STORAGE_FTS

#include "data_plane/fts/ob_fts_parser_name.h"

#include <cctype>

namespace oceanbase
{
namespace storage
{

int ObFTParserName::set_name(const char *name)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(name)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("parser name is null", K(ret));
  } else if (OB_UNLIKELY(STRLEN(name) >= OB_FT_PARSER_NAME_LENGTH)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("parser name is too long", K(ret), KCSTRING(name));
  } else {
    int64_t i = 0;
    for (; '\0' != name[i]; ++i) {
      name_[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    }
    name_[i] = '\0';
  }
  return ret;
}

int ObFTParserName::set_name(const common::ObString &name)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(name.empty() || name.length() >= OB_FT_PARSER_NAME_LENGTH)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("parser name is invalid", K(ret), K(name));
  } else {
    common::ObString::obstr_size_t i = 0;
    for (; i < name.length() && '\0' != name[i]; ++i) {
      name_[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    }
    name_[i] = '\0';
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
