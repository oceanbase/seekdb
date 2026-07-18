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

#define USING_LOG_PREFIX SQL_EXE

#include "sql/executor/ob_direct_transmit_op.h"

namespace oceanbase
{
namespace sql
{
using namespace oceanbase::common;

int ObDirectTransmitOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(child_->get_next_row()) && OB_ITER_END != ret) {
    LOG_WARN("get next row from child failed", K(ret));
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObDirectTransmitOpInput, ObTransmitOpInput));

OB_SERIALIZE_MEMBER((ObDirectTransmitSpec, ObTransmitSpec));

} /* ns sql */
} /* ns oceanbase */
