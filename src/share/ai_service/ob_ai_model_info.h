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

// ai model plain value types(EndpointType/ObAiServiceModelInfo):depends only on lib,logical layer L2
// made layer-neutral from ob_ai_service_struct.h,for lower layers such as ob_rpc_struct.h
#ifndef OCEANBASE_SHARE_AI_SERVICE_OB_AI_MODEL_INFO_H_
#define OCEANBASE_SHARE_AI_SERVICE_OB_AI_MODEL_INFO_H_

#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace common
{
class ObIJsonBase;
}
namespace share
{

struct EndpointType final
{
  enum TYPE : uint8_t
  {
    INVALID_TYPE = 0,
    DENSE_EMBEDDING = 1,
    SPARSE_EMBEDDING = 2,
    COMPLETION = 3,
    RERANK = 4,
    // add new endpoint type before this line
    // also remember to add ENDPOINT_TYPE_STR
    MAX_TYPE
  };
  static EndpointType::TYPE str_to_endpoint_type(const ObString &type_str);
  static EndpointType::TYPE convert_type_from_int(const int64_t type)
  {

    EndpointType::TYPE endpoint_type = EndpointType::INVALID_TYPE;
    if (type >= static_cast<int64_t>(EndpointType::INVALID_TYPE) && type <= static_cast<int64_t>(EndpointType::MAX_TYPE)) {
      endpoint_type = static_cast<EndpointType::TYPE>(type);
    }
    return endpoint_type;
  }
private:
  static const char *ENDPOINT_TYPE_STR[];
};

class ObAiServiceModelInfo
{
  OB_UNIS_VERSION(1);
public:
  ObAiServiceModelInfo() { reset(); }
  ~ObAiServiceModelInfo() = default;

  void reset()
  {
    name_.reset();
    type_ = EndpointType::MAX_TYPE;
    model_name_.reset();
  }

  int parse_from_json_base(const ObString &name, const common::ObIJsonBase &params_jbase);
  int check_valid() const;

  const ObString &get_name() const { return name_; }
  EndpointType::TYPE get_type() const { return type_; }
  const ObString &get_model_name() const { return model_name_; }

  TO_STRING_KV(K_(name),
               K_(type),
               K_(model_name));
private:
  ObString name_;
  EndpointType::TYPE type_;
  ObString model_name_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_AI_SERVICE_OB_AI_MODEL_INFO_H_
