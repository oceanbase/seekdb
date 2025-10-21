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

#ifndef OCEANBASE_BASIC_OB_VALUES_OP_H_
#define OCEANBASE_BASIC_OB_VALUES_OP_H_


#include "common/row/ob_row_store.h"
#include "sql/engine/ob_operator.h"

namespace oceanbase
{
namespace sql
{

//
// Values operator is only use in explain statement to hold the explain result.
//
class ObValuesSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObValuesSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);

  common::ObRowStore row_store_;
};

class ObValuesOp : public ObOperator
{
public:
  ObValuesOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);

  virtual int inner_open() override;
  virtual int inner_rescan() override;

  virtual int inner_get_next_row() override;

  virtual void destroy() override { ObOperator::destroy(); }

private:
  common::ObNewRow cur_row_;
  common::ObRowStore::Iterator row_store_it_;
};

} // end namespace sql
} // end namespace oceanbase
#endif // OCEANBASE_BASIC_OB_VALUES_OP_H_
