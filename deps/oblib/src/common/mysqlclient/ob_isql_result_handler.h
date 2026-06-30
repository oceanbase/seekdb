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

#ifndef OCEANBASE_MYSQLCLIENT_OB_ISQL_RESULT_HANDLER_H_
#define OCEANBASE_MYSQLCLIENT_OB_ISQL_RESULT_HANDLER_H_

namespace oceanbase
{
namespace common
{
namespace sqlclient
{

class ObMySQLResult;

class ObISQLResultHandler
{
public:
  ObISQLResultHandler() {}
  virtual ~ObISQLResultHandler() {}

  virtual ObMySQLResult *mysql_result() = 0;
};

} // end namespace sqlclient
} // end namespace common
} // end namespace oceanbase


#endif // OCEANBASE_MYSQLCLIENT_OB_ISQL_RESULT_HANDLER_H_
