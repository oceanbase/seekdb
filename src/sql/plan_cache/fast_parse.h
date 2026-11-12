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

#pragma once

#include "lib/allocator/ob_allocator.h"
#include "lib/string/ob_string.h"
#include "common/sql_mode/ob_sql_mode.h"
#include "sql/plan_cache/ob_plan_cache_struct.h"

namespace oceanbase
{
namespace sql
{
struct FPContext
{
  bool enable_batched_multi_stmt_ = false;
  ObSQLMode sql_mode_ = 0;
  bool is_format_ = false;
  bool parameterize_ = true;
  bool ignore_hint_ = false;
};

struct ObFastParser final
{
public:
  static int parse(const common::ObString &stmt,
                   const FPContext &context,
                   common::ObIAllocator &allocator,
                   ObFastParserResult &result);
  static int compress_insert(common::ObIAllocator &allocator,
                                        int64_t values_token_pos,
                                        const ObString &old_no_param_sql,
                                        ObString &new_truncated_sql,
                                        bool &can_batch_opt,
                                        int64_t &params_count,
                                        int64_t &upd_params_count,
                                        int64_t &lenth_delta,
                                        int64_t &row_count,
                                        bool &is_insert_values);

};

} // namespace sql
} // namespace oceanbase
