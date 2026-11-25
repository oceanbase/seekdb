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

#ifndef UNITTEST_SQL_ENGINE_SORT_OB_BASE_SORT_H_
#define UNITTEST_SQL_ENGINE_SORT_OB_BASE_SORT_H_

#include "common/row/ob_row_iterator.h"
#include "common/object/ob_obj_type.h"
#include "sql/ob_sql_define.h"

namespace oceanbase
{
namespace sql
{
struct ObSortColumnExtra
{
  ObSortColumnExtra()
    : obj_type_(common::ObMaxType), order_type_(default_asc_direction()) {}
  ObSortColumnExtra(common::ObObjType obj_type, ObOrderDirection order_type)
    : obj_type_(obj_type), order_type_(order_type) {}

  TO_STRING_KV(K_(obj_type), K_(order_type));
  common::ObObjType obj_type_;
  ObOrderDirection order_type_;
};

class ObSortColumn : public common::ObColumnInfo, public ObSortColumnExtra
{
public:
  ObSortColumn()
    : ObColumnInfo(),
      ObSortColumnExtra(),
      is_ascending_(false)
  {}
  ObSortColumn(int64_t index,
               common::ObCollationType cs_type,
               bool is_asc)
    : ObSortColumnExtra(), is_ascending_(is_asc)
  {
    index_ = index;
    cs_type_ = cs_type;
  }
  ObSortColumn(int64_t index,
               common::ObCollationType cs_type,
               bool is_asc,
               common::ObObjType obj_type,
               ObOrderDirection order_type)
    : ObSortColumnExtra(obj_type, order_type), is_ascending_(is_asc)
  {
    index_ = index;
    cs_type_ = cs_type;
  }
  bool is_ascending() const { return is_ascending_; }
  void set_is_ascending(bool is_ascending) { is_ascending_ = is_ascending; }
  inline bool is_null_first() const {
    return NULLS_FIRST_ASC == order_type_
           || NULLS_FIRST_DESC == order_type_;
  }
  inline ObOrderDirection get_order_type() const {
    return order_type_;
  }
  inline common::ObObjType get_obj_type() const {
    return obj_type_;
  }
  inline common::ObCmpNullPos get_cmp_null_pos() const {
    common::ObCmpNullPos ret_pos = common::default_null_pos();
    switch (order_type_) {
    case NULLS_FIRST_ASC: {
      ret_pos = common::NULL_FIRST;
    } break;
    case NULLS_FIRST_DESC: {
      ret_pos = common::NULL_LAST;
    } break;
    case NULLS_LAST_ASC: {
      ret_pos = common::NULL_LAST;
    } break;
    case NULLS_LAST_DESC: {
      ret_pos = common::NULL_FIRST;
    } break;
    default:
      break;
    }
    return ret_pos;
  }
  TO_STRING_KV3(N_INDEX_ID, index_,
                N_COLLATION_TYPE, common::ObCharset::collation_name(cs_type_),
                N_ASCENDING, is_ascending() ? N_ASC : N_DESC,
                "ObjType", obj_type_,
                "OrderType", order_type_);
private:
  bool is_ascending_;
};

} // namespace sql
} // namespace oceanbase

#endif /* UNITTEST_SQL_ENGINE_SORT_OB_BASE_SORT_H_ */
