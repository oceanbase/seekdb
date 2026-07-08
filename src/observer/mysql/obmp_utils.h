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

#ifndef _OBMP_UTILS_H_
#define _OBMP_UTILS_H_
#include <stdint.h>
#include "sql/monitor/flt/ob_flt_control_info_mgr.h"

namespace oceanbase
{
namespace obmysql
{
class OMPKOK;
}
namespace sql
{
class ObSQLSessionInfo;
}
namespace common
{
class ObTimeZoneInfo;
class ObString;
class ObIAllocator;
class ObObj;
}
namespace observer
{
class ObMPUtils
{
public:
  static int add_changed_session_info(obmysql::OMPKOK &ok_pkt, sql::ObSQLSessionInfo &session);
  static int add_nls_format(obmysql::OMPKOK &pk_pkt,
                            sql::ObSQLSessionInfo &session,
                            const bool only_changed = false);
private:
  static int get_user_sql_literal(common::ObIAllocator &allocator, const common::ObObj &obj,
                                  common::ObString &value_str, const common::ObObjPrintParams &print_param);
  static int get_literal_print_length(const common::ObObj &obj, bool is_plain, int64_t &len,
                                      const common::ObObjPrintParams &print_param);
};
} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OBMP_UTILS_H_ */
