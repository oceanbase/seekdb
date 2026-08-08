#!/usr/bin/env
# -*- coding: UTF-8 -*-

import os

DEFINED_COLLS = [
    "CS_TYPE_BINARY",
    "CS_TYPE_UTF8MB4_GENERAL_CI",
    "CS_TYPE_UTF8MB4_BIN",
    ]

compile_template = '''/*
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

#include "ob_str_datum_funcs_compilation.ipp"

namespace oceanbase
{
namespace common
{
%COMPILE_FUN_LIST%
} // end common
} // end oceanbase
'''

common_template = '''
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

#ifndef OCEANBASE_STR_DATUM_FUNCS_IPP
#define OCEANBASE_STR_DATUM_FUNCS_IPP

#include "share/datum/ob_datum_funcs.h"
#include "share/datum/ob_datum_cmp_func_def.h"
#include "common/object/ob_obj_funcs.h"
#include "share/vector/ob_bit_vector.h"
#include "share/ob_version_parser.h"
#include "share/datum/ob_datum_funcs_impl.h"

namespace oceanbase
{
using namespace sql;
namespace common
{

#define DEF_STR_FUNC_INIT(COLLATION, unit_idx)                                                 \\
  void __init_str_func##unit_idx()                                                             \\
  {                                                                                            \\
    str_cmp_initer<COLLATION>::init_array();                                                   \\
    str_basic_initer<COLLATION, 0>::init_array();                                              \\
    str_basic_initer<COLLATION, 1>::init_array();                                              \\
  }

} // end common
} // end oceanbase
#endif // OCEANBASE_STR_DATUM_FUNCS_IPP'''

COMPILE_UNIT_CNT = 8

def rm_compile_part():
  rm_str = "rm -rf ob_str_datum_funcs_compilation_*.cpp"
  rm_str2 = "rm -rf ob_str_datum_funcs_compilation.ipp"
  rm_str3 = "rm -rf ob_str_datum_funcs_all.cpp"
  os.system(rm_str)
  os.system(rm_str2)
  os.system(rm_str3)


def generate_compile_parts():
  fname_temp = "ob_str_datum_funcs_compilation_%d.cpp"
  fn_cnt = int((len(DEFINED_COLLS) + COMPILE_UNIT_CNT  - 1) / COMPILE_UNIT_CNT)
  fn_list_text = ""
  for i in range(fn_cnt):
    fn_list_text += "DEF_STR_FUNC_INIT(%COLL_NAME" + str(i) + "%, %unit_idx" + str(i) + "%);\n" 
  for start in range(0, len(DEFINED_COLLS), fn_cnt):
    text = compile_template.replace("%COMPILE_FUN_LIST%", fn_list_text)
    for i in range(fn_cnt):
      coll_temp = "%COLL_NAME" + str(i) + "%"
      idx_temp = "%unit_idx" + str(i) + "%"
      if start + i >= len(DEFINED_COLLS):
        text = text.replace(coll_temp, "CS_TYPE_MAX")
      else:
        text = text.replace(coll_temp, DEFINED_COLLS[start + i])
      text = text.replace(idx_temp, str(start + i))
    f_name = fname_temp % (start // fn_cnt)
    with open(f_name, 'a') as f:
      f.write(text)


def generate_ctrl_part():
  ctrl_text = '''/*
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
#include "lib/charset/ob_charset.h"
namespace oceanbase
{
namespace common
{
'''
  for i in range(0, len(DEFINED_COLLS)):
    ctrl_text += "extern void __init_str_func%d();\n" % i

  ctrl_text += "void __init_all_str_funcs() {\n"

  for i in range(0, len(DEFINED_COLLS)):
    ctrl_text += "  __init_str_func%d();\n" % i
  
  ctrl_text += '''}
} // end common
} // end oceanbase
'''

  with open("ob_str_datum_funcs_all.cpp", 'a') as f:
    f.write(ctrl_text)


def generate_common():
  with open("ob_str_datum_funcs_compilation.ipp", 'a') as f:
    f.write(common_template)
  
 

if __name__ == "__main__":
  rm_compile_part()
  generate_common()
  generate_compile_parts()
  generate_ctrl_part()
