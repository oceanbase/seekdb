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

#ifndef OCEANBASE_SHARE_OB_VERSION_PARSER_H_
#define OCEANBASE_SHARE_OB_VERSION_PARSER_H_

#include <stdint.h>
#include "common/ob_version_def.h"

namespace oceanbase
{
namespace common
{
class ObString;

class ObVersionParser
{
public:
  static int is_valid(const char *verstr);
  static int get_version(const char *verstr, uint64_t &version);
  static int get_version(const common::ObString &verstr, uint64_t &version);
  static int64_t print_vsn(char *buf, const int64_t buf_len, uint64_t version);
  static int64_t print_version_str(char *buf, const int64_t buf_len, uint64_t version);
public:
  static const int64_t MAX_VERSION_ITEM = 16;
  static const int64_t MAJOR_POS       = 0;
  static const int64_t MINOR_POS       = 1;
  static const int64_t MAJOR_PATCH_POS = 2;
  static const int64_t MINOR_PATCH_POS = 3;
};

// the version definition is moved to deps/oblib/src/common/ob_version_def.h

} // end of namespace common
} // end of namespace oceanbase

#endif /* OCEANBASE_SHARE_OB_VERSION_PARSER_H_ */
