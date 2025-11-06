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

#ifndef OB_WIDE_INTEGER_CMP_FUNCS_
#define OB_WIDE_INTEGER_CMP_FUNCS_

// #include "lib/wide_integer/ob_wide_integer.h"

// typedef int (*decint_obj_cmp_fp)(const oceanbase::common::ObObj &lhs,
//                                  const oceanbase::common::ObObj &rhs);
typedef int (*decint_cmp_fp)(const oceanbase::common::ObDecimalInt *lhs,
                             const oceanbase::common::ObDecimalInt *rhs);
namespace oceanbase
{
namespace common
{
namespace datum_cmp
{
template<ObDecimalIntWideType l, ObDecimalIntWideType r>
struct ObDecintCmp;
} // namespace datum_cmp

namespace wide
{

class ObDecimalIntCmpSet
{
public:
  static const constexpr int32_t DECIMAL_LEN = 64;
  static inline decint_cmp_fp get_decint_decint_cmp_func(int32_t len1, int32_t len2)
  {
    if (len1 > DECIMAL_LEN || len2 > DECIMAL_LEN) {
      return nullptr;
    } else {
      return decint_decint_cmp_set_[len1][len2];
    }
  }
private:
  template<int32_t, int32_t>
  friend struct InitDecimalIntCmpSet;

  template<ObDecimalIntWideType l, ObDecimalIntWideType r>
  friend struct datum_cmp::ObDecintCmp;

  static decint_cmp_fp decint_decint_cmp_set_[DECIMAL_LEN + 1][DECIMAL_LEN +1];
};

// TODO: unittest
// decimal int comparison, scale must be raised to the same level first
// helper functions
template<typename T, typename P>
int compare(const T &lhs, const P &rhs, int &result)
{
  int ret = OB_SUCCESS;
  const ObDecimalInt *lhs_decint = lhs.get_decimal_int();
  const ObDecimalInt *rhs_decint = rhs.get_decimal_int();
  int32_t lhs_bytes = lhs.get_int_bytes();
  int32_t rhs_bytes = rhs.get_int_bytes();
  decint_cmp_fp cmp = ObDecimalIntCmpSet::get_decint_decint_cmp_func(lhs_bytes, rhs_bytes);
  if (OB_ISNULL(cmp)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "failed to get compare function", K(ret), K(lhs_bytes), K(rhs_bytes));
  } else {
    result = cmp(lhs_decint, rhs_decint);
  }
  return ret;
}

template<typename T, typename P>
bool abs_equal(const T &lhs, const P &rhs)
{
#define ABS_CMP(ltype, rtype)                                                                      \
  const ltype &lv = *reinterpret_cast<const ltype *>(lhs_decint);                                  \
  const rtype &rv = *reinterpret_cast<const rtype *>(rhs_decint);                                  \
  is_equal = (((lv + rv) == 0) || (lv == rv));

  int ret = OB_SUCCESS;
  bool is_equal = false;
  const ObDecimalInt *lhs_decint = lhs.get_decimal_int();
  const ObDecimalInt *rhs_decint = rhs.get_decimal_int();
  int32_t lhs_bytes = lhs.get_int_bytes();
  int32_t rhs_bytes = rhs.get_int_bytes();

  DISPATCH_INOUT_WIDTH_TASK(lhs_bytes, rhs_bytes, ABS_CMP);
  return is_equal;
#undef ABS_CMP
}
} // end namespace wide
} // end namespace common
} // end namespace oceanbase
#endif // !OB_WIDE_INTEGER_CMP_FUNCS_
