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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_COL_DESC_H_
#define OCEANBASE_SHARE_SCHEMA_OB_COL_DESC_H_

#include <stdint.h>
#include "common/object/ob_object.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "share/ob_define.h"
#include "share/schema/ob_schema_struct.h"  // ObSkipIndexColumnAttr

namespace oceanbase
{
namespace share
{
namespace schema
{

struct ObColDesc final
{
public:
  ObColDesc():col_id_(0), col_type_(), col_order_(common::ObOrderType::ASC) {};
  ~ObColDesc() = default;
  int64_t to_string(char *buffer, const int64_t length) const
  {
    int64_t pos = 0;
    (void)common::databuff_printf(buffer, length, pos, "column_id=%u ", col_id_);
    pos += col_type_.to_string(buffer + pos, length - pos);
    (void)common::databuff_printf(buffer, length, pos, " order=%d", col_order_);
    return pos;
  }
  int assign(const ObColDesc &other)
  {
    int ret = common::OB_SUCCESS;
    if (this != &other) {
      col_id_ = other.col_id_;
      col_type_ = other.col_type_;
      col_order_ = other.col_order_;
    }
    return ret;
  }
  void reset();

  NEED_SERIALIZE_AND_DESERIALIZE;

  uint32_t col_id_;
  common::ObObjMeta col_type_;
  common::ObOrderType col_order_;
};

struct ObColExtend final
{
  OB_UNIS_VERSION(1);
public:
  ObColExtend(): skip_index_attr_() {};
  ~ObColExtend() = default;
  int64_t to_string(char *buffer, const int64_t length) const
  {
    int64_t pos = 0;
    pos += skip_index_attr_.to_string(buffer + pos, length - pos);
    return pos;
  }
  int assign(const ObColExtend &other)
  {
    int ret = common::OB_SUCCESS;
    if (this != &other) {
      skip_index_attr_ = other.skip_index_attr_;
    }
    return ret;
  }
  void reset();
private:
  DISALLOW_COPY_AND_ASSIGN(ObColExtend);
public:
  ObSkipIndexColumnAttr skip_index_attr_;
};

}  // namespace schema
}  // namespace share
}  // namespace oceanbase

#endif /* OCEANBASE_SHARE_SCHEMA_OB_COL_DESC_H_ */
