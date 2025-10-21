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
