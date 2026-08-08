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

#ifndef OCEANBASE_DATA_PLANE_API_OB_I_TX_CALLBACK_H_
#define OCEANBASE_DATA_PLANE_API_OB_I_TX_CALLBACK_H_

namespace oceanbase
{
namespace transaction
{

// Completion port implemented by query-side callbacks and invoked by the
// transaction implementation.  It deliberately carries only the completion
// status so neither side needs the other's implementation types.
class ObITxCallback
{
public:
  virtual void callback(int ret) = 0;
};

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_I_TX_CALLBACK_H_
