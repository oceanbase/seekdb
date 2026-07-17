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

#ifndef OCEANBASE_OBSERVER_MYSQL_OBMP_STMT_RESET_H_
#define OCEANBASE_OBSERVER_MYSQL_OBMP_STMT_RESET_H_

#include "observer/ob_server_struct.h"
#include "observer/mysql/obmp_base.h"

namespace oceanbase
{
namespace observer
{

class ObMPStmtReset : public ObMPBase
{
public:
  static const obmysql::ObMySQLCmd COM = obmysql::COM_STMT_RESET;
public:
  explicit ObMPStmtReset(const ObGlobalContext &gctx)
      : ObMPBase(gctx), stmt_id_(common::OB_INVALID_STMT_ID)
  {}
  virtual ~ObMPStmtReset() {}

protected:
  int deserialize();
  int process();
private:
  common::ObPsStmtId stmt_id_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObMPStmtReset);
};



} //end of namespace observer
} //end of namespace oceanbase

#endif //OCEANBASE_OBSERVER_MYSQL_OBMP_STMT_RESET_H_
