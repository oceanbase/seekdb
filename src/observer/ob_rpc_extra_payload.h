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

#ifndef OCEANBASE_OBSERVER_OB_RPC_EXTRA_PAYLOAD_H_
#define OCEANBASE_OBSERVER_OB_RPC_EXTRA_PAYLOAD_H_

#include "rpc/obrpc/ob_irpc_extra_payload.h"

namespace oceanbase
{
namespace observer
{

class ObRpcExtraPayload : public obrpc::ObIRpcExtraPayload
{
public:
  ObRpcExtraPayload() {}
  virtual ~ObRpcExtraPayload() {}

  virtual int64_t get_serialize_size() const override;
  virtual int serialize(SERIAL_PARAMS) const override;
  virtual int deserialize(DESERIAL_PARAMS) override;

  static ObRpcExtraPayload &extra_payload_instance()
  {
    static ObRpcExtraPayload global_rpc_extra_payload;
    return global_rpc_extra_payload;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(ObRpcExtraPayload);
};

} // end namespace server
} // end namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_RPC_EXTRA_PAYLOAD_H_
