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

#include "ob_i_weak_read_service.h"


namespace oceanbase
{
namespace transaction
{

const char *wrs_level_to_str(const int level)
{
  const char *str = "INVALID";
  switch (level) {
    case WRS_LEVEL_CLUSTER:
      str = "CLUSTER";
      break;
    case WRS_LEVEL_REGION:
      str = "REGION";
      break;
    case WRS_LEVEL_ZONE:
      str = "ZONE";
      break;
    case WRS_LEVEL_SERVER:
      str = "SERVER";
      break;
    default:
      str = "UNKONWN";
  }
  return str;
}

}
}
