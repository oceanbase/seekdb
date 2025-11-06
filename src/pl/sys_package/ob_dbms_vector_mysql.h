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

#include "pl/ob_pl.h"

namespace oceanbase
{
namespace pl
{

class ObDBMSVectorMySql
{
public:
  ObDBMSVectorMySql() {}
  virtual ~ObDBMSVectorMySql() {}

#define DECLARE_FUNC(func) \
  static int func(ObPLExecCtx &ctx, sql::ParamStore &params, common::ObObj &result);

  DECLARE_FUNC(refresh_index);
  DECLARE_FUNC(rebuild_index);
  DECLARE_FUNC(refresh_index_inner);
  DECLARE_FUNC(rebuild_index_inner);
  DECLARE_FUNC(index_vector_memory_advisor);
  DECLARE_FUNC(index_vector_memory_estimate);

#undef DECLARE_FUNC

  static int parse_idx_param(const ObString &idx_type_str,
                             const ObString &idx_param_str,
                             uint32_t dim_count,
                             ObVectorIndexParam &index_param);

private:
  static int get_estimate_memory_str(ObVectorIndexParam index_param,
                                     uint64_t num_vectors,
                                     uint64_t tablet_max_num_vectors,
                                     ObStringBuffer &res_buf);
  static int print_mem_size(uint64_t mem_size, ObStringBuffer &res_buf);
};

} // namespace pl
} // namespace oceanbase
