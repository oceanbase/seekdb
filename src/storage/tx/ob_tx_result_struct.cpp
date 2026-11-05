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

#define USING_LOG_PREFIX TRANS
#include "storage/tx/ob_tx_result_struct.h"

namespace oceanbase
{
namespace obcall
{
DEF_TO_STRING(ObCreateTabletBatchInTransRes)
{
  int64_t pos = 0;
  J_KV(K_(ret), K_(tx_result));
  return pos;
}

OB_SERIALIZE_MEMBER(ObCreateTabletBatchInTransRes, ret_, tx_result_);

void ObRegisterTxDataResult::reset()
{
  result_ = OB_SUCCESS;
  tx_result_.reset();
  return;
}

OB_SERIALIZE_MEMBER(ObRegisterTxDataResult, result_, tx_result_);

}  // namespace obcall
}  // namespace oceanbase
