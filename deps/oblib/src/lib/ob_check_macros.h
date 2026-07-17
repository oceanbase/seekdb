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
// common OB error-checking/argument-checking macros(CK/OX/OZ/OC/OV series)。pure preprocessing、only uses lib-level LOG_WARN/OB_SUCC/
// OB_ERR_UNEXPECTED/CONCAT/ARGS_NUM, kept in lib for full-stack sharing; previously in src/share/ob_define.h。
#ifndef OCEANBASE_LIB_OB_CHECK_MACROS_H_
#define OCEANBASE_LIB_OB_CHECK_MACROS_H_
#include "lib/utility/ob_macro_utils.h"  // CONCAT / ARGS_NUM

#define CK_1(a1)\
  CK_0(#a1, a1)
#define CK_2(a1, a2) \
  CK_1(a1) else CK_0(#a2, a2)
#define CK_3(a1, a2, a3) \
  CK_2(a1, a2) else CK_0(#a3, a3)
#define CK_4(a1, a2, a3, a4) \
  CK_3(a1, a2, a3) else CK_0(#a4, a4)
#define CK_5(a1, a2, a3, a4, a5) \
  CK_4(a1, a2, a3, a4) else CK_0(#a5, a5)
#define CK_6(a1, a2, a3, a4, a5, a6) \
  CK_5(a1, a2, a3, a4, a5) else CK_0(#a6, a6)
#define CK_7(a1, a2, a3, a4, a5, a6, a7) \
  CK_6(a1, a2, a3, a4, a5, a6) else CK_0(#a7, a7)
#define CK_8(a1, a2, a3, a4, a5, a6, a7, a8) \
  CK_7(a1, a2, a3, a4, a5, a6, a7) else CK_0(#a8, a8)
#define CK_9(a1, a2, a3, a4, a5, a6, a7, a8, a9) \
  CK_8(a1, a2, a3, a4, a5, a6, a7, a8) else CK_0(#a9, a9)
#define CK_10(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
  CK_9(a1, a2, a3, a4, a5, a6, a7, a8, a9) else CK_0(#a10, a10)
#define CK_11(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
  CK_10(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) else CK_0(#a11, a11)
#define CK_12(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
  CK_11(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) else CK_0(#a12, a12)
#define CK_13(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
  CK_12(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) else CK_0(#a13, a13)
#define CK_14(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
  CK_13(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) else CK_0(#a14, a14)
#define CK_15(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
  CK_14(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) else CK_0(#a15, a15)
#define CK_16(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16) \
  CK_15(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) else CK_0(#a16, a16)

#define CK_0(a, b)                              \
  if (!(b)) {                                   \
    if (OB_SUCC(ret)) {                         \
      ret = OB_ERR_UNEXPECTED;                  \
    }                                           \
    LOG_WARN("invalid arguments", a, b);        \
  }

// Reference document:
// Check every argument and stop to print if anyone of them is false
#define CK(...)                                                         \
  if (OB_SUCC(ret)) { CONCAT(CK_, ARGS_NUM(__VA_ARGS__))(__VA_ARGS__) }

// Reference document:
// execute an instruction
#define OX(statement)                           \
  if (OB_SUCC(ret)) {                           \
    statement;                                  \
  }


/*

  Reference documentation:

  This better be the last macro we ever need to define in the
  O-series, hence 'Z'.

  OZ( f(a1, a2, a3) );          // print ret in case of failure
  OZ( f(a1, a2, a3), a3 );      // print ret, a3 in case of failure
  OZ( f(a1, a2, a3), a2, a3 );  // print ret, a2, a3 in case of failure
*/
#define OZ(func, ...) OC_I5(func, OC_I4(__VA_ARGS__))


#define OC_I4(...) ret, ##__VA_ARGS__
#define OC_I3(...) OC_I4(__VA_ARGS__)
#define OC_I(func) OC_I1(func,
#define OC_I1(func, a) OC_I2(func, a)
#define KK(a) K(a)
#define OC_I2(func, a)                            \
  if (OB_SUCC(ret)) {                             \
    if (OB_FAIL(func a)) {                        \
      LOG_WARN("fail to exec "#func #a,           \
               LST_DO(KK, (,), OC_I3(EXPAND a))); \
    }                                             \
  }

// Should be combined with OC_I2...
#define OC_I5(func, a)                          \
  do {                                          \
    if (OB_SUCC(ret)) {                         \
      if (OB_FAIL(func)) {                      \
        LOG_WARN("fail to exec "#func,          \
                 LST_DO(KK, (,), a));           \
      }                                         \
    }                                           \
  } while(0)

/*
  Extension of OZ, add a retcode, when the return value is retcode, use log info instead of log warn to reduce unnecessary screen flooding
  eg:
  OZX1( f(a1, a2, a3), OB_ERR_NO_PRIVILEGE);          // print ret in case of failure
  OZX1( f(a1, a2, a3), OB_ERR_NO_PRIVILEGE, a3 );      // print ret, a3 in case of failure
  OZX1( f(a1, a2, a3), OB_ERR_NO_PRIVILEGE, a2, a3 );  // print ret, a2, a3 in case of failure
*/
#define OZX1(func, ret_code, ...) OCX1_I5(func, ret_code, OC_I4(__VA_ARGS__))

// Should be combined with OC_I2...
#define OCX1_I5(func, ret_code, a)              \
  do {                                          \
    if (OB_SUCC(ret)) {                         \
      if (OB_FAIL(func)) {                      \
        if (ret == ret_code) {                  \
          LOG_DEBUG("fail to exec "#func,       \
                   LST_DO(KK, (,), a));         \
        } else {                                \
          LOG_WARN("fail to exec "#func,        \
                   LST_DO(KK, (,), a));         \
        }                                       \
      }                                         \
    }                                           \
  } while(0)

/*
  Extension of OZ, add a retcode, when the return value is retcode, use log info instead of log warn to reduce unnecessary screen flooding
  OZX2( f(a1, a2, a3), OB_ERR_NO_PRIVILEGE, OB_ERR_EMPTY_QUERY); // print ret in case of failure
  OZX2( f(a1, a2, a3), OB_ERR_NO_PRIVILEGE, OB_ERR_EMPTY_QUERY, a3 );
  OZX2( f(a1, a2, a3), OB_ERR_NO_PRIVILEGE, OB_ERR_EMPTY_QUERY, a2, a3 );
*/
#define OZX2(func, ret_code1, ret_code2, ...) OCX2_I5(func, ret_code1, ret_code2, OC_I4(__VA_ARGS__))

// Should be combined with OC_I2...
#define OCX2_I5(func, ret_code1, ret_code2, a)  \
  do {                                          \
    if (OB_SUCC(ret)) {                         \
      if (OB_FAIL(func)) {                      \
        if (ret == ret_code1                    \
            || ret == ret_code2) {              \
          LOG_DEBUG("fail to exec "#func,       \
                   LST_DO(KK, (,), a));         \
        } else {                                \
          LOG_WARN("fail to exec "#func,        \
                   LST_DO(KK, (,), a));         \
        }                                       \
      }                                         \
    }                                           \
  } while(0)

#define OV(...) \
  CONCAT(OV_, ARGS_NUM(__VA_ARGS__))(__VA_ARGS__)

#define OV_0()                         OV_I5(false, common::OB_ERR_UNEXPECTED)
#define OV_1(condition)                OV_I5(condition, common::OB_ERR_UNEXPECTED)
#define OV_2(condition, errcode)       OV_I5(condition, errcode)
#define OV_3(condition, errcode, ...)  OV_I5(condition, errcode, __VA_ARGS__)
#define OV_4(condition, errcode, ...)  OV_I5(condition, errcode, __VA_ARGS__)
#define OV_5(condition, errcode, ...)  OV_I5(condition, errcode, __VA_ARGS__)
#define OV_6(condition, errcode, ...)  OV_I5(condition, errcode, __VA_ARGS__)
#define OV_7(condition, errcode, ...)  OV_I5(condition, errcode, __VA_ARGS__)
#define OV_8(condition, errcode, ...)  OV_I5(condition, errcode, __VA_ARGS__)
#define OV_9(condition, errcode, ...)  OV_I5(condition, errcode, __VA_ARGS__)

#define OV_I4(...) ret, ##__VA_ARGS__
#define OV_I5(condition, errcode, ...)                \
  if (common::OB_SUCCESS == (ret)) {                  \
    if (OB_UNLIKELY(!(condition))) {                  \
      ret = (errcode);                                \
      LOG_WARN("fail to check ("#condition")",        \
               LST_DO(KK, (,), OV_I4(__VA_ARGS__)));  \
    }                                                 \
  }

// run the specified function and print out every argument
// in case of failure (obsoleted)
#define OC(func) OC_I func )

#endif // OCEANBASE_LIB_OB_CHECK_MACROS_H_
