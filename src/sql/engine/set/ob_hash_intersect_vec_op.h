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

#ifndef OCEANBASE_BASIC_OB_SET_OB_HASH_INTERSECT_VEC_OP_H_
#define OCEANBASE_BASIC_OB_SET_OB_HASH_INTERSECT_VEC_OP_H_

#include "sql/engine/set/ob_hash_set_vec_op.h"

namespace oceanbase
{
namespace sql
{

class ObHashIntersectVecSpec : public ObHashSetVecSpec
{
OB_UNIS_VERSION_V(1);
public:
  ObHashIntersectVecSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);
};

class ObHashIntersectVecOp : public ObHashSetVecOp
{
public:
  ObHashIntersectVecOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);
  ~ObHashIntersectVecOp() {}

  virtual int inner_open() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override;
  virtual int inner_close() override;
  virtual int inner_rescan() override;
  virtual void destroy() override;

private:
  int build_hash_table_by_part(int64_t batch_size);
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_BASIC_OB_SET_OB_HASH_INTERSECT_VEC_OP_H_
