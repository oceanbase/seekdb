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

#ifndef OCEANBASE_LIB_OB_STRINGS_H_
#define OCEANBASE_LIB_OB_STRINGS_H_

#include "lib/string/ob_string.h"
#include "lib/container/ob_array.h"
#include "common/ob_string_buf.h"

namespace oceanbase
{
namespace common
{
/**
 * an array of strings
 *
 */
class ObStrings
{
public:
  ObStrings();
  virtual ~ObStrings();
  int add_string(const ObString &str, int64_t *idx = NULL);
  int get_string(int64_t idx, ObString &str) const;
  int64_t count() const;
  void reuse();

  int64_t to_string(char *buf, const int64_t buf_len) const;
  NEED_SERIALIZE_AND_DESERIALIZE;

private:
  ObStringBuf buf_;
  ObArray<ObString> strs_;

  DISALLOW_COPY_AND_ASSIGN(ObStrings);
};
} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_LIB_OB_STRINGS_H_
