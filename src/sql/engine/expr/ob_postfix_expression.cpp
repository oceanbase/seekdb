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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_postfix_expression.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

int ObPostExprItem::assign(const common::ObObj &obj)
{
  int ret = OB_SUCCESS;
  ObItemType item_type = static_cast<ObItemType>((obj.get_type()));
  if (OB_UNLIKELY(!IS_DATATYPE_OR_QUESTIONMARK_OP(item_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid obj type", K(ret), K(obj));
  } else {
    new(&v2_.v1_) ObObj(obj);
    item_type_ = item_type;
  }
  return ret;
}

int ObPostExprItem::set_column(int64_t index)
{
  int ret = OB_SUCCESS;
  if (index < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid index", K(ret), K(index));
  } else {
    item_type_ = T_REF_COLUMN;
    v2_.cell_index_ = index;
  }
  return ret;
}

int ObPostExprItem::assign(ObExprOperator *op)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(op)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("op is NULL", K(ret));
  } else {
    item_type_ = op->get_type();
    v2_.op_ = op;
  }
  return ret;
}

int ObPostExprItem::assign(ObItemType item_type)
{
  int ret = OB_SUCCESS;
  if (T_INVALID == item_type) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("item type is invalid", K(ret));
  } else {
    item_type_ = item_type;
  }
  return ret;
}

/* for unittest only */

/* for unittest only */
/* so need NOT normalize */

int64_t ObPostExprItem::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (IS_DATATYPE_OP(item_type_)) {
    J_OW(J_KV(N_CONST, get_obj(),
              N_ACCURACY, accuracy_));
  } else {
    switch (item_type_) {
      case T_REF_COLUMN: {
        J_OW(J_KV(N_COLUMN_INDEX, get_column(),
                  N_ACCURACY, accuracy_));
        break;
      }
      case T_QUESTIONMARK: {
        J_OW(J_KV(N_PARAM, get_obj().get_int(),
                  N_ACCURACY, accuracy_));
        break;
      }
      default: {
        if (IS_EXPR_OP(item_type_)) {
          J_OW(J_KV(N_OP, *get_expr_operator()));
        } else {
          LOG_WARN_RET(OB_ERR_UNEXPECTED, "unknown item", K_(item_type));
        }
        break;
      }
    } // end switch
  }

  return pos;
}

DEFINE_SERIALIZE(ObPostExprItem)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(item_type_);
  if (OB_SUCC(ret)) {
    if (T_REF_COLUMN == item_type_) {
      OB_UNIS_ENCODE(v2_.cell_index_);
      OB_UNIS_ENCODE(accuracy_);
    } else if (IS_DATATYPE_OR_QUESTIONMARK_OP(item_type_)) {
      ObObj tmp = get_obj();
      OB_UNIS_ENCODE(tmp);
      OB_UNIS_ENCODE(accuracy_);
    } else if (IS_EXPR_OP(item_type_)) {
      OB_UNIS_ENCODE(*v2_.op_);
    } else {
      ret = OB_UNKNOWN_OBJ;
      LOG_ERROR("Unknown expr item to serialize", K(ret), K_(item_type));
    }
  }
  return ret;
}

int ObPostExprItem::deserialize(ObIAllocator &alloc,
                                const char *buf,
                                const int64_t data_len,
                                int64_t &pos)
{
  int ret = OB_SUCCESS;
  OB_UNIS_DECODE(item_type_);
  if (OB_SUCC(ret)) {
    if (T_REF_COLUMN == item_type_) {
      OB_UNIS_DECODE(v2_.cell_index_);
      OB_UNIS_DECODE(accuracy_);
    } else if (IS_DATATYPE_OR_QUESTIONMARK_OP(item_type_)) {
      ObObj tmp;
      OB_UNIS_DECODE(tmp);
      ObObj local_mem_obj;
      if (OB_FAIL(deep_copy_obj(alloc, tmp, local_mem_obj))) {
      } else {
        new(&v2_.v1_) ObObj(local_mem_obj);
        OB_UNIS_DECODE(accuracy_);
      }
    } else if (IS_EXPR_OP(item_type_)) {
      ObExprOperatorFactory factory(alloc);
      if (OB_FAIL(factory.alloc(item_type_, v2_.op_))) {
      } else if (OB_ISNULL(v2_.op_)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("failed to allc expr operator", K(ret), K_(item_type));
      } else {
        OB_UNIS_DECODE(*v2_.op_);
      }
    } else {
      ret = OB_UNKNOWN_OBJ;
      LOG_ERROR("Unknown expr item to deserialize", K(ret), K_(item_type));
    }
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObPostExprItem)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(item_type_);
  if (T_REF_COLUMN == item_type_) {
    OB_UNIS_ADD_LEN(v2_.cell_index_);
    OB_UNIS_ADD_LEN(accuracy_);
  } else if (IS_DATATYPE_OR_QUESTIONMARK_OP(item_type_)) {
    ObObj tmp = get_obj();
    OB_UNIS_ADD_LEN(tmp);
    OB_UNIS_ADD_LEN(accuracy_);
  } else if (IS_EXPR_OP(item_type_)) {
    OB_UNIS_ADD_LEN(*v2_.op_);
  } else {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "Unknown expr item to serialize", K_(item_type));
  }
  return len;
}

} // namespace sql
} // namespace oceanbase
