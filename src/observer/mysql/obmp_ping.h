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

#ifndef OCEANBASE_OBSERVER_MYSQL_OBMP_PING_H_
#define OCEANBASE_OBSERVER_MYSQL_OBMP_PING_H_

#include "observer/mysql/obmp_base.h"

namespace oceanbase
{
namespace observer
{

class ObMPPing : public ObMPBase
{
public:
  static const obmysql::ObMySQLCmd COM = obmysql::COM_PING;
  explicit ObMPPing(const ObGlobalContext &gctx);
  virtual ~ObMPPing();

protected:
  int process();
  int deserialize();

private:
  common::ObString sql_;
  DISALLOW_COPY_AND_ASSIGN(ObMPPing);
}; // end of class ObMPPing

} // end of namespace observer
} // end of namespace oceanbase


#endif // OCEANBASE_OBSERVER_MYSQL_OBMP_PING_H_
