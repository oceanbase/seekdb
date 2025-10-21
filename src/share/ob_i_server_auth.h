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

#ifndef OCEANBASE_SHARE_OB_I_SERVER_AUTH_H_
#define OCEANBASE_SHARE_OB_I_SERVER_AUTH_H_
#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace share
{
class ObIServerAuth
{
public:
  ObIServerAuth() {}
  virtual ~ObIServerAuth() {}
  virtual int is_server_legitimate(const common::ObAddr& addr, bool& is_valid) = 0;
  virtual void set_ssl_invited_nodes(const common::ObString &new_value) = 0;
};
}; // end namespace share
}; // end namespace oceanbase

#endif /* OCEANBASE_SHARE_OB_I_SERVER_AUTH_H_ */

