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

#ifndef OCEANBASE_BASIC_OB_TOPK_OP_H_
#define OCEANBASE_BASIC_OB_TOPK_OP_H_

#include "sql/engine/ob_operator.h"

namespace oceanbase
{
namespace sql
{

class ObTopKSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObTopKSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);

  bool is_valid() const;

  INHERIT_TO_STRING_KV("op_spec", ObOpSpec, K_(minimum_row_count), K_(topk_precision),
                        KPC_(org_limit), KPC_(org_offset));

  int64_t minimum_row_count_;
  int64_t topk_precision_;
  ObExpr *org_limit_;
  ObExpr *org_offset_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTopKSpec);
};

class ObTopKOp : public ObOperator
{
public:
  ObTopKOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);

  virtual int inner_open() override;
  virtual int inner_rescan() override;

  virtual int inner_get_next_row() override;

  virtual void destroy() override { ObOperator::destroy(); }

private:
  // According to child_ and limit/offset set topk_final_count_
  // Only will be called after rescan or after the first get_next_row()
  int get_topk_final_count();

  DISALLOW_COPY_AND_ASSIGN(ObTopKOp);

  int64_t topk_final_count_;
  int64_t output_count_;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_BASIC_OB_TOPK_OP_H_
