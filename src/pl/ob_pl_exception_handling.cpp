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

#define USING_LOG_PREFIX PL
#include "ob_pl_exception_handling.h"

namespace oceanbase
{
using namespace common;

namespace pl
{

ObPLConditionType ObPLEH::eh_classify_exception(const char *sql_state)
{
  ObPLConditionType type = INVALID_TYPE;
  if (NULL != sql_state) {
    if ('0' == sql_state[0] && '0' == sql_state[1]) {
      type = INVALID_TYPE;
    } else if ('0' == sql_state[0] && '1' == sql_state[1]) {
      type = SQL_WARNING;
    } else if ('0' == sql_state[0] && '2' == sql_state[1]) {
      type = NOT_FOUND;
    } else {
      type = SQL_EXCEPTION;
    }
  }
  return type;
}

}  // namespace pl
}  // namespace oceanbase
