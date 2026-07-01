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

#ifndef OCEANBASE_BASIC_OB_BATCH_RESULT_HOLDER_H_
#define OCEANBASE_BASIC_OB_BATCH_RESULT_HOLDER_H_

#include "lib/container/ob_se_array.h"
#include "lib/allocator/page_arena.h"
#include "share/datum/ob_datum.h"
#include "sql/engine/expr/ob_expr.h"

namespace oceanbase
{
namespace sql
{

// Batch result backup && restore, backup rows by shadow copy.
class ObBatchResultHolder
{
public:
  ObBatchResultHolder() : allocator_(NULL), exprs_(NULL), eval_ctx_(NULL), datums_(NULL),
                          saved_size_(0), inited_(false)
  {
  }

  int init(const common::ObIArray<ObExpr *> &exprs, ObEvalCtx &eval_ctx,
           common::ObIAllocator *alloctor = nullptr);
  int save(int64_t size);
  int restore();
  bool is_saved() const { return saved_size_ > 0; }
  void reset() { saved_size_ = 0; }
  void destroy();
  int check_datum_modified();

private:
  common::ObIAllocator *allocator_;
  const common::ObIArray<ObExpr *> *exprs_;
  ObEvalCtx *eval_ctx_;
  ObDatum *datums_;
  int64_t saved_size_;
  bool inited_;
};


} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_BASIC_OB_BATCH_RESULT_HOLDER_H_
