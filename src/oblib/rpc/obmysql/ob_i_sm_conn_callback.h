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

#ifndef OCEANBASE_OBMYSQL_OB_I_SM_CONN_CALLBACK_H_
#define OCEANBASE_OBMYSQL_OB_I_SM_CONN_CALLBACK_H_
namespace oceanbase
{
namespace observer
{
class ObSMConnection;
};

namespace obmysql
{
class ObSqlSockSession;
class ObISMConnectionCallback
{
public:
  ObISMConnectionCallback() {}
  virtual ~ObISMConnectionCallback() {}
  virtual int init(ObSqlSockSession& sess, observer::ObSMConnection& conn) = 0;
  virtual void destroy(observer::ObSMConnection& conn) = 0;
  virtual int on_disconnect(observer::ObSMConnection& conn) = 0;
};

}; // end namespace obmysql
}; // end namespace oceanbase

#endif /* OCEANBASE_OBMYSQL_OB_I_SM_CONN_CALLBACK_H_ */

