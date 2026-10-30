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

#ifndef OCEANBASE_COMMON_OB_CMP_OP_H_
#define OCEANBASE_COMMON_OB_CMP_OP_H_

namespace oceanbase
{
namespace common
{

enum ObCmpOp
{
  CO_EQ = 0,
  CO_LE,
  CO_LT,
  CO_GE,
  CO_GT,
  CO_NE,
  CO_CMP,
  CO_MAX
};

inline bool ob_is_valid_cmp_op(ObCmpOp cmp_op) { return 0 <= cmp_op && cmp_op < CO_MAX; }
inline bool ob_is_invalid_cmp_op(ObCmpOp cmp_op) { return !ob_is_valid_cmp_op(cmp_op); }
inline bool ob_is_valid_cmp_op_bool(ObCmpOp cmp_op) { return 0 <= cmp_op && cmp_op < CO_CMP; }
inline bool ob_is_invalid_cmp_op_bool(ObCmpOp cmp_op) { return !ob_is_valid_cmp_op_bool(cmp_op); }

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_COMMON_OB_CMP_OP_H_
