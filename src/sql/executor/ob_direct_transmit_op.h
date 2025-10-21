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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_DIRECT_TRANSMIT_OP_
#define OCEANBASE_SQL_EXECUTOR_OB_DIRECT_TRANSMIT_OP_

#include "sql/engine/px/exchange/ob_transmit_op.h"

namespace oceanbase
{
namespace sql
{

class ObDirectTransmitOpInput : public ObTransmitOpInput
{
  OB_UNIS_VERSION_V(1);
public:
  ObDirectTransmitOpInput(ObExecContext &ctx, const ObOpSpec &spec)
    : ObTransmitOpInput(ctx, spec)
  {}
  virtual ~ObDirectTransmitOpInput() {};
  virtual int init(ObTaskInfo &task_info) override
  {
    UNUSED(task_info);
    return common::OB_SUCCESS;
  }
private:
  DISALLOW_COPY_AND_ASSIGN(ObDirectTransmitOpInput);
};

class ObDirectTransmitSpec : public ObTransmitSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObDirectTransmitSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
  : ObTransmitSpec(alloc, type)
  {}

  virtual ~ObDirectTransmitSpec() {};
};

class ObDirectTransmitOp : public ObTransmitOp
{
public:
  ObDirectTransmitOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObTransmitOp(exec_ctx, spec, input) {}
  virtual ~ObDirectTransmitOp() {}
  virtual int inner_get_next_row();
private:
  DISALLOW_COPY_AND_ASSIGN(ObDirectTransmitOp);
};

}
}
#endif /*  OCEANBASE_SQL_EXECUTOR_OB_DIRECT_TRANSMIT_OP_ */
//// end of header file
