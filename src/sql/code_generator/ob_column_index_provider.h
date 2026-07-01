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

#ifndef _OB_COLUMN_INDEX_PROVIDER_H
#define _OB_COLUMN_INDEX_PROVIDER_H
#include "lib/hash/ob_hashmap.h"
#include "lib/container/ob_se_array.h"
#include "lib/allocator/ob_mod_define.h"

namespace oceanbase
{
namespace sql
{

class ObRawExpr;
class RowDesc
{
public:
  RowDesc() {}
  virtual ~RowDesc() {}
  int init();
  void reset();
  int assign(const RowDesc &other);
  int append(const RowDesc &other);
  /**
   * @brief add column to row descriptor
   */
  int add_column(ObRawExpr *raw_expr);
  int64_t get_column_num() const;
  ObRawExpr *get_column(int64_t idx) const;
  int get_column(int64_t idx, ObRawExpr *&raw_expr) const;
  int get_idx(const ObRawExpr *raw_expr, int64_t &idx) const;
  TO_STRING_KV(N_EXPR, exprs_);
private:
  typedef common::hash::ObHashMap<int64_t, int64_t, common::hash::NoPthreadDefendMode> ExprIdxMap;
  ExprIdxMap expr_idx_map_;
  common::ObSEArray<ObRawExpr*, 64> exprs_;
  DISALLOW_COPY_AND_ASSIGN(RowDesc);
};

} // end namespace sql
} // end namespace oceanbase
#endif
