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

#ifndef OCEANBASE_ROOT_MYSQL_PROXY_UTIL_H_
#define OCEANBASE_ROOT_MYSQL_PROXY_UTIL_H_

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class ObISQLConnectionPool;
}
// mysql lib function utility class
class ObMySQLProxyUtil
{
public:
  ObMySQLProxyUtil();
  virtual ~ObMySQLProxyUtil();
public:
  // init the connection pool
  // escape the old string convert from to to
private:
  sqlclient::ObISQLConnectionPool *pool_;
};
}
}

#endif // OCEANBASE_ROOT_MYSQL_PROXY_UTIL_H_
