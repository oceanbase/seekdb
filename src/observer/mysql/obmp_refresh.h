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

#ifndef OCEANBASE_OBSERVER_MYSQL_OBMP_REFRESH_H_
#define OCEANBASE_OBSERVER_MYSQL_OBMP_REFRESH_H_

#include "observer/mysql/obmp_base.h"

namespace oceanbase
{
namespace observer
{

class ObMPRefresh : public ObMPBase
{
public:
  static const obmysql::ObMySQLCmd COM = obmysql::COM_REFRESH;
  explicit ObMPRefresh(const ObGlobalContext &gctx);
  virtual ~ObMPRefresh();

protected:
  int process();
  int deserialize();

private:
  DISALLOW_COPY_AND_ASSIGN(ObMPRefresh);
}; // end of class ObMPRefresh

} // end of namespace observer
} // end of namespace oceanbase


#endif // OCEANBASE_OBSERVER_MYSQL_OBMP_REFRESH_H_
