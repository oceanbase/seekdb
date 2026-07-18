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

#include "share/schema/ob_col_desc.h"

namespace oceanbase
{
using namespace common;
namespace share
{
namespace schema
{

/************************************* ObColDesc **********************************/
void ObColDesc::reset()
{
  col_id_ = UINT32_MAX;
  col_type_.reset();
  col_order_ = common::ObOrderType::ASC;
}

DEFINE_SERIALIZE(ObColDesc)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(col_id_);
  OB_UNIS_ENCODE(col_type_);
  OB_UNIS_ENCODE(col_order_);
  return ret;
}

DEFINE_DESERIALIZE(ObColDesc)
{
  int ret = OB_SUCCESS;
  OB_UNIS_DECODE(col_id_);
  OB_UNIS_DECODE(col_type_);
  OB_UNIS_DECODE(col_order_);
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObColDesc)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(col_id_);
  OB_UNIS_ADD_LEN(col_type_);
  OB_UNIS_ADD_LEN(col_order_);
  return len;
}

/************************************* ObColExtend **********************************/
void ObColExtend::reset()
{
  skip_index_attr_.reset();
}

OB_DEF_SERIALIZE(ObColExtend)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, skip_index_attr_);
  return ret;
}

OB_DEF_DESERIALIZE(ObColExtend)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, skip_index_attr_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObColExtend)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, skip_index_attr_);
  return len;
}

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
