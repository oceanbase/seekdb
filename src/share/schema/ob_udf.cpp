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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_udf.h"

namespace oceanbase
{

using namespace std;
using namespace common;

namespace share
{
namespace schema
{

ObUDF::ObUDF(common::ObIAllocator *allocator)
    : ObSchema(allocator), tenant_id_(common::OB_INVALID_ID), udf_id_(common::OB_INVALID_ID), name_(), ret_(UDFRetType::UDF_RET_UNINITIAL),
      dl_(), type_(UDFType::UDF_TYPE_UNINITIAL), schema_version_(common::OB_INVALID_VERSION)
{
  reset();
}

ObUDF::ObUDF(const ObUDF &src_schema)
    : ObSchema(), tenant_id_(common::OB_INVALID_ID), udf_id_(common::OB_INVALID_ID), name_(), ret_(UDFRetType::UDF_RET_UNINITIAL),
      dl_(), type_(UDFType::UDF_TYPE_UNINITIAL), schema_version_(common::OB_INVALID_VERSION)
{
  reset();
  *this = src_schema;
}

ObUDF::~ObUDF()
{
}

ObUDF &ObUDF::operator = (const ObUDF &src_schema)
{
  if (this != &src_schema) {
    reset();
    error_ret_ = src_schema.error_ret_;
    tenant_id_ = src_schema.tenant_id_;
    ret_ = src_schema.ret_;
    type_ = src_schema.type_;

    int ret = OB_SUCCESS;
    if (OB_FAIL(deep_copy_str(src_schema.name_, name_))) {
      LOG_WARN("Fail to deep copy udf name, ", K(ret));
    } else if (OB_FAIL(deep_copy_str(src_schema.dl_, dl_))) {
      LOG_WARN("Fail to deep copy udf dl, ", K(ret));
    } else {/*do nothing*/}

    if (OB_FAIL(ret)) {
      error_ret_ = ret;
    }
    LOG_DEBUG("operator =", K(src_schema), K(*this));
  }
  return *this;
}

bool ObUDF::operator==(const ObUDF &r) const
{
  return (tenant_id_ == r.tenant_id_ && name_ == r.name_);
}


int64_t ObUDF::get_convert_size() const
{
  int64_t convert_size = sizeof(*this);
  convert_size += name_.length() + 1;
  convert_size += dl_.length() + 1;
  return convert_size;
}

void ObUDF::reset()
{
  tenant_id_ = OB_INVALID_ID;
  name_.reset();
  ret_ = STRING;
  dl_.reset();
  type_ = FUNCTION;
  ObSchema::reset();
}

OB_SERIALIZE_MEMBER(ObUDF,
										tenant_id_,
										name_,
										ret_,
										dl_,
										type_);



OB_SERIALIZE_MEMBER(ObUDFMeta,
                    tenant_id_,
                    name_,
                    ret_,
                    dl_,
                    type_);

}
}
}
