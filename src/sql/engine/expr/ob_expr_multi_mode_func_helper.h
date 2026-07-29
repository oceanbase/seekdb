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

#ifndef OCEANBASE_SQL_OB_EXPR_MULTI_MODE_FUNC_HELPER_H_
#define OCEANBASE_SQL_OB_EXPR_MULTI_MODE_FUNC_HELPER_H_

#include "sql/session/ob_sql_session_info.h"
#include "lib/allocator/page_arena.h"

namespace oceanbase
{
namespace sql
{

class MultimodeAlloctor : public ObIAllocator
{
public:
  explicit MultimodeAlloctor(ObArenaAllocator &arena);
  ~MultimodeAlloctor() = default;

public:
  virtual void *alloc(const int64_t sz);
  void *alloc(const int64_t size, const ObMemAttr &attr);
  virtual void *realloc(void *ptr, const int64_t oldsz, const int64_t newsz) { return arena_.realloc(ptr, oldsz, newsz); }
  virtual void free(void *ptr) { arena_.free(ptr); }
  int64_t used() const { return arena_.used(); }
  int64_t total() const { return arena_.total(); }
  void reset() { arena_.reset(); }
  int eval_arg(const ObExpr *arg, ObEvalCtx &ctx, common::ObDatum *&datum);
private:
  ObIAllocator &arena_;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_OB_EXPR_MULTI_MODE_FUNC_HELPER_H_
