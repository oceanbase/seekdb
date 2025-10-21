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

#ifndef OB_VERSION_H
#define OB_VERSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
  const char *build_version();
  const char *build_date();
  const char *build_time();
  const char *build_flags();
  const char *build_branch();
  const char *build_info();
#ifdef __cplusplus
}
#endif

namespace oceanbase
{
namespace share
{
int get_package_and_svn(char *server_version, int64_t buf_len);
}
}

#endif /* OB_VERSION_H */
