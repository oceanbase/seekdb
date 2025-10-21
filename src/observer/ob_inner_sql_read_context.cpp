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

#include "ob_inner_sql_read_context.h"

namespace oceanbase
{
namespace observer
{

ObInnerSQLReadContext::ObInnerSQLReadContext(ObInnerSQLConnection &conn)
    : conn_ref_(conn), vt_iter_factory_(*conn.get_vt_iter_creator()),
      result_(conn.get_session(), conn.is_inner_session(), conn.get_diagnostic_info())
{
}

ObInnerSQLReadContext::~ObInnerSQLReadContext()
{
  if (this == conn_ref_.get_conn().get_prev_read_ctx()) {
    conn_ref_.get_conn().get_prev_read_ctx() = NULL;
  }
}

} // end of namespace observer
} // end of namespace oceanbase
