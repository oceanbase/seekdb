/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OBLIB_LDS_ASSIST_H
#define OBLIB_LDS_ASSIST_H

#include "lib/utility/ob_macro_utils.h"

/*
  Interfaces related to the auxiliary linker script
  The basic idea is to utilize the section sorting functionality of the linker script (by name),
  Define two special begin and end section nodes,
  After dictionary order sorting, the objects between these two nodes are the target nodes

  Assuming the section is mydata, the address layout after linking is as follows:
  var_name         section_name(before link)   section_name(after link)
  ------------------------------------------------------------
  mydata_begin             mydata               mydata
  a                        mydata1              mydata
  b                        mydata2              mydata
  c                        mydata3              mydata
  mydata_end               mydataz              mydata

  It is also worth noting that here begin/end do not use the common practice of defining only at link time,
  But are directly defined at compile time, this is to solve the dynamic linking problem, still taking the above a, b, c as an example,
  In dynamic linking, multiple extern variables with the same name in different shared objects are resolved to the one in the main program, the linker cannot see a, b, c in the so when linking the main program, thus causing the begin and end addresses to be the same
*/

#define LDS_ATTRIBUTE_(section_name) __attribute__((section(#section_name)))
#define LDS_ATTRIBUTE(section_name) LDS_ATTRIBUTE_(section_name)

/*
  Declare and define a global variable within section
  eg.
     static LDS_VAR(mydata, int, i);
     extern LDS_VAR(mydata, int, i);
*/
#ifndef OB_USE_ASAN
#define LDS_VAR(section_name, type, name) \
  type name LDS_ATTRIBUTE(CONCAT(section_name, __COUNTER__))
#else
#define LDS_VAR(section_name, type, name) \
  type name
#endif

#define LDS_VAR_BEGIN_END(section_name, type)                   \
  static type section_name##_begin LDS_ATTRIBUTE(section_name); \
  static type section_name##_end LDS_ATTRIBUTE(section_name##z)

template<typename T, typename Func>
inline void do_ld_iter(T *s, T *e, Func &func)
{
  T *p = s;
  while (p < e) {
    /*
      @param 1: index
      @param 2: addr
    */
    func(p - s, p);
    p++;
  }
}

/*
  Traverse all global variables within the section, and call under the namespace defined by LDS_VAR_BEGIN_END
*/
#define LDS_ITER(section_name, type, func) \
  do_ld_iter((type*)&section_name##_begin + 1, (type*)&section_name##_end, func)

#endif /* OBLIB_LDS_ASSIST_H */