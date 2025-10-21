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

#ifndef OCEANBASE_OBRPC_OB_IRPC_EXTRA_PAYLOAD_H_
#define OCEANBASE_OBRPC_OB_IRPC_EXTRA_PAYLOAD_H_

#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace obrpc
{

class ObIRpcExtraPayload
{
public:
  virtual ~ObIRpcExtraPayload() {}

  virtual int64_t get_serialize_size() const = 0;
  virtual int serialize(SERIAL_PARAMS) const = 0;
  virtual int deserialize(DESERIAL_PARAMS) = 0;

  static inline ObIRpcExtraPayload &instance() { return *instance_pointer(); }
  // not thread safe
  static void set_extra_payload(ObIRpcExtraPayload &extra_payload)
  {
    instance_pointer() = &extra_payload;
  }

private:
  static inline ObIRpcExtraPayload *&instance_pointer();
};

class ObEmptyExtraPayload : public ObIRpcExtraPayload
{
public:
  virtual int64_t get_serialize_size() const override { return 0; }
  virtual int serialize(SERIAL_PARAMS) const override { UNF_UNUSED_SER; return common::OB_SUCCESS; }
  virtual int deserialize(DESERIAL_PARAMS) override { UNF_UNUSED_DES; return common::OB_SUCCESS; }

  static ObEmptyExtraPayload &empty_instance()
  {
    static ObEmptyExtraPayload global_empty_extra_payload;
    return global_empty_extra_payload;
  }
};

inline ObIRpcExtraPayload *&ObIRpcExtraPayload::instance_pointer()
{
  static ObIRpcExtraPayload *global_rpc_extra_payload = &ObEmptyExtraPayload::empty_instance();
  return global_rpc_extra_payload;
}

} // end namespace obrpc
} // end namespace oceanbase

#endif // OCEANBASE_OBRPC_OB_IRPC_EXTRA_PAYLOAD_H_
