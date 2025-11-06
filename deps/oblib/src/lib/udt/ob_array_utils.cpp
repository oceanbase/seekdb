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

#define USING_LOG_PREFIX LIB
#include "ob_array_utils.h"

namespace oceanbase {
namespace common {

int ObArrayUtil::get_type_name(ObNestedType coll_type, const ObDataType &elem_type, char *buf, int buf_len, uint32_t depth)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  for (uint32_t i = 0; OB_SUCC(ret) && i < depth; i++) {
    if (coll_type == ObNestedType::OB_ARRAY_TYPE) {
      if (OB_FAIL(databuff_printf(buf, buf_len, pos, "ARRAY("))) {
        LOG_WARN("failed to convert len to string", K(ret));
      }
    } else if (coll_type == ObNestedType::OB_VECTOR_TYPE) {
      if (OB_FAIL(databuff_printf(buf, buf_len, pos, "VECTOR("))) {
        LOG_WARN("failed to convert len to string", K(ret));
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid collection type", K(ret), K(coll_type));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%s", ob_sql_type_str(elem_type.get_obj_type())))) {
    LOG_WARN("failed to convert len to string", K(ret));
  } else if (elem_type.get_obj_type() == ObDecimalIntType
             && OB_FAIL(databuff_printf(buf, buf_len, pos, "(%d,%d)", elem_type.get_precision(), elem_type.get_scale()))) {
    LOG_WARN("failed to add deciaml precision to string", K(ret));
  } else if (ob_is_string_tc(elem_type.get_obj_type())
             && OB_FAIL(databuff_printf(buf, buf_len, pos, "(%d)", elem_type.get_length()))) {
    LOG_WARN("failed to add string len to string", K(ret));
  } 
  for (uint32_t i = 0; OB_SUCC(ret) && i < depth; i++) {
    if (OB_FAIL(databuff_printf(buf, buf_len, pos, ")"))) {
      LOG_WARN("failed to add ) to string", K(ret));
    }
  }
  return ret;
}


// convert collection bin to string (for liboblog)

// determine a collection type is vector, array or map

#define FIXED_SIZE_ARRAY_APPEND(Element_Type, Get_Func)                                               \
  ObArrayFixedSize<Element_Type> *array_obj = static_cast<ObArrayFixedSize<Element_Type> *>(&array);  \
  if (OB_FAIL(array_obj->push_back(datum->Get_Func()))) {                                             \
    LOG_WARN("failed to push back value", K(ret));                                                    \
  }

int ObArrayUtil::append(ObIArrayType &array, const ObObjType elem_type, const ObDatum *datum)
{
  int ret = OB_SUCCESS;
  if (datum->is_null()) {
    if (OB_FAIL(array.push_null())) {
      LOG_WARN("failed to push back null value", K(ret));
    }
  } else {
    switch (elem_type) {
      case ObNullType: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("expect null value", K(ret));
        break;
      }
      case ObTinyIntType: {
        FIXED_SIZE_ARRAY_APPEND(int8_t, get_tinyint);
        break;
      }
      case ObSmallIntType: {
        FIXED_SIZE_ARRAY_APPEND(int16_t, get_smallint);
        break;
      }
      case ObInt32Type: {
        FIXED_SIZE_ARRAY_APPEND(int32_t, get_int32);
        break;
      }
      case ObIntType: {
        FIXED_SIZE_ARRAY_APPEND(int64_t, get_int);
        break;
      }
      case ObUTinyIntType: {
        FIXED_SIZE_ARRAY_APPEND(uint8_t, get_utinyint);
        break;
      }
      case ObUSmallIntType: {
        FIXED_SIZE_ARRAY_APPEND(uint16_t, get_usmallint);
        break;
      }
      case ObUInt32Type: {
        FIXED_SIZE_ARRAY_APPEND(uint32_t, get_uint32);
        break;
      }
      case ObUInt64Type: {
        FIXED_SIZE_ARRAY_APPEND(uint64_t, get_uint64);
        break;
      }
      case ObUFloatType:
      case ObFloatType: {
        FIXED_SIZE_ARRAY_APPEND(float, get_float);
        break;
      }
      case ObUDoubleType:
      case ObDoubleType: {
        FIXED_SIZE_ARRAY_APPEND(double, get_double);
        break;
      }
      case ObVarcharType: {
        ObArrayBinary *binary_array = static_cast<ObArrayBinary *>(&array);
        if (OB_FAIL(binary_array->push_back(datum->get_string()))) {
          LOG_WARN("failed to push back null value", K(ret));
        }
        break;
      }
      default:
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("unsupported element type", K(ret), K(elem_type));
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
