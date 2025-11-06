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

#ifndef OB_UTILITY_H_
#define OB_UTILITY_H_

#include <stdint.h>
#include <time.h>

namespace oceanbase
{
namespace common
{
int64_t lower_align(int64_t input, int64_t align);
int64_t upper_align(int64_t input, int64_t align);
int64_t ob_pwrite(const int fd, const char *buf, const int64_t count, const int64_t offset);
int64_t ob_pread(const int fd, char *buf, const int64_t count, const int64_t offset);
int mprotect_page(const void *mem_ptr, int64_t len, int prot, const char *addr_name);
}
}
#endif /* OB_UTILITY_H_ */
