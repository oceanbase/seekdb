
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
#include "sql/engine/ob_serializable_function.h"
#include "sql/engine/ob_bit_vector.h"
#include "share/ob_cluster_version.h"
#include "share/datum/ob_datum_funcs_impl.h"

namespace oceanbase
{
using namespace sql;
namespace common
{

#define DEF_STR_FUNC_INIT(COLLATION, unit_idx)                                                 \
  void __init_str_func##unit_idx()                                                             \
  {                                                                                            \
    str_cmp_initer<COLLATION>::init_array();                                                   \
    str_basic_initer<COLLATION, 0>::init_array();                                              \
    str_basic_initer<COLLATION, 1>::init_array();                                              \
  }

} // end common
} // end oceanbase
#endif // OCEANBASE_STR_DATUM_FUNCS_IPP
