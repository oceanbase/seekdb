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

#ifndef OCEANBASE_OBSERVER_MYSQL_OBMP_PROCESS_INFO_H_
#define OCEANBASE_OBSERVER_MYSQL_OBMP_PROCESS_INFO_H_

#include "observer/mysql/obmp_query.h"

namespace oceanbase
{
namespace observer
{

class ObMPProcessInfo : public ObMPQuery
{
public:
  //static const obmysql::ObMySQLCmd COM = obmysql::COM_PROCESS_INFO;
  explicit ObMPProcessInfo(const ObGlobalContext &gctx);
  virtual ~ObMPProcessInfo();
protected:
  int deserialize() override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMPProcessInfo);
}; // end of class ObMPProcessInfo

} // end of namespace observer
} // end of namespace oceanbase


#endif // OCEANBASE_OBSERVER_MYSQL_OBMP_PROCESS_INFO_H_
