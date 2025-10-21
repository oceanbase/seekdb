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

#ifndef OCEANBASE_SHARE_MOCK_MYSQL_PROXY_H_
#define OCEANBASE_SHARE_MOCK_MYSQL_PROXY_H_

#include <gmock/gmock.h>
#include "lib/mysqlclient/ob_mysql_proxy.h"

namespace oceanbase
{
namespace common
{

class MockMySQLProxy : public ObMySQLProxy
{
public:
  MOCK_METHOD2(read, int(ObMySQLProxy::ReadResult &, const char *));
  MOCK_METHOD3(read, int(ObMySQLProxy::ReadResult &, const uint64_t, const char *));

  MOCK_METHOD2(write, int(const char *, int64_t &));
  MOCK_METHOD3(write, int(const uint64_t, const char *, int64_t &));
};

} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_MOCK_MYSQL_PROXY_H_
