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

#ifndef OCEANBAE_LIB_OB_TRUNCATED_STRING_H_
#define OCEANBAE_LIB_OB_TRUNCATED_STRING_H_

#include "lib/string/ob_string.h"
#include "share/config/ob_server_config.h"

namespace oceanbase
{
namespace common
{

class ObTruncatedString
{
public:
  ObTruncatedString(const ObString &str, const int32_t limit)
  {
    const int32_t len = (limit < str.length()) ? (limit < 0 ? 0 : limit) : str.length();
    str_ = ObString(len, str.ptr());
  }

  explicit ObTruncatedString(const ObString &str)
  {
    const int32_t len = (GCONF.max_string_print_length < str.length()) ?
                    static_cast<int32_t>(GCONF.max_string_print_length) : str.length();
    str_ = ObString(len, str.ptr());
  }
  inline int32_t length() const { return str_.length(); }
  inline int32_t size() const { return str_.size(); }
  inline const char *ptr() const { return str_.ptr(); }
  inline const ObString & string() const { return str_; }

  int64_t to_string(char *buf, const int64_t len) const
  {
    int64_t pos = 0;
    if (OB_ISNULL(buf) || len <= 0) {
      // do nothing
    } else {
      pos = str_.to_string(buf, len);
    }
    return pos;
  }

private:
  ObString str_;

  DISALLOW_COPY_AND_ASSIGN(ObTruncatedString);
};

}/* ns common*/
}/* ns oceanbase */

#endif // OCEANBAE_LIB_OB_TRUNCATED_STRING_H_


