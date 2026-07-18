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

#include "lib/ob_name_id_def.h"
#include <string.h>
namespace oceanbase
{
namespace name
{
static const char* ID_NAMES[NAME_COUNT+1];
static const char* ID_DESCRIPTIONS[NAME_COUNT+1];
struct RuntimeIdNameMapInit
{
  RuntimeIdNameMapInit()
  {
#define DEF_NAME(name_sym, description) ID_NAMES[name_sym] = #name_sym;
#define DEF_NAME_PAIR(name_sym, description) \
  DEF_NAME(name_sym ## _begin, description " begin")    \
  DEF_NAME(name_sym ## _end, description " end")

#include "ob_name_id_def.h"
#undef DEF_NAME
#undef DEF_NAME_PAIR
#define DEF_NAME(name_sym, description) ID_DESCRIPTIONS[name_sym] = description;
#define DEF_NAME_PAIR(name_sym, description) \
  DEF_NAME(name_sym ## _begin, description " begin")    \
  DEF_NAME(name_sym ## _end, description " end")
#include "ob_name_id_def.h"
#undef DEF_NAME
#undef DEF_NAME_PAIR
  }
};
static RuntimeIdNameMapInit INIT;

const char* get_name(int32_t id)
{
  const char* ret = NULL;
  if (id < NAME_COUNT && id >= 0) {
    ret = oceanbase::name::ID_NAMES[id];
  }
  return ret;
}


} // end namespace name_id_map
} // end namespace oceanbase
