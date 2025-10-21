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

#ifndef EXPRJ_STRING_CMP_H
#define EXPRJ_STRING_CMP_H

namespace oceanbase {
namespace jit {
namespace expr {
extern int jit_strcmpsp(const char *str1,
             int64_t str1_len,
             const char *str2,
             int64_t str2_len,
             bool cmp_endspace);
}  // expr
}  // jit
}  // oceanbase

#endif
