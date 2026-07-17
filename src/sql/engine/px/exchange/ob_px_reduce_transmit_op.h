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

#ifndef OCEANBASE_ENGINE_PX_EXCHANGE_OB_PX_REDUCE_TRANSMIT_OP_H_
#define OCEANBASE_ENGINE_PX_EXCHANGE_OB_PX_REDUCE_TRANSMIT_OP_H_

#include "sql/engine/px/exchange/ob_px_transmit_op.h"

namespace oceanbase
{
namespace sql
{

class ObPxReduceTransmitOpInput : public ObPxTransmitOpInput
{
public:
  OB_UNIS_VERSION_V(1);
public:
  ObPxReduceTransmitOpInput(ObExecContext &ctx, const ObOpSpec &spec)
    : ObPxTransmitOpInput(ctx, spec)
  {}
  virtual ~ObPxReduceTransmitOpInput()
  {}
};

class ObPxReduceTransmitSpec : public ObPxTransmitSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObPxReduceTransmitSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
  : ObPxTransmitSpec(alloc, type)
  {}
  ~ObPxReduceTransmitSpec() {}
};

class ObPxReduceTransmitOp : public ObPxTransmitOp
{
public:
  ObPxReduceTransmitOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
  : ObPxTransmitOp(exec_ctx, spec, input)
  {}
  virtual ~ObPxReduceTransmitOp() {}
public:
  virtual int inner_open() override;
  virtual int inner_rescan() override { return ObPxTransmitOp::inner_rescan(); }
  virtual void destroy() override {return ObPxTransmitOp::destroy(); }
  virtual int inner_close() override;

  virtual int do_transmit() override;
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_ENGINE_PX_EXCHANGE_OB_PX_REDUCE_TRANSMIT_OP_H_
