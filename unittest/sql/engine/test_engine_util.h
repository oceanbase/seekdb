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

#ifndef OCEANBASE_ENGINE_TEST_ENGINE_UTIL_H_
#define OCEANBASE_ENGINE_TEST_ENGINE_UTIL_H_

#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{

inline int create_test_session(ObExecContext &ctx)
{
  int ret = 0;
  if (!ctx.get_my_session()) {
    ObSQLSessionInfo *s = new ObSQLSessionInfo;
    if (OB_FAIL(s->test_init(0, 123456789, 123456789, NULL))) {
      delete s;
      return ret;
    } else {
      ctx.set_my_session(s);
    }
  }
  return 0;
}

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_ENGINE_TEST_ENGINE_UTIL_H_
