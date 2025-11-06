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

#ifndef _OBMP_SET_OPTION_H_
#define _OBMP_SET_OPTION_H_

#include "observer/mysql/obmp_base.h"

namespace oceanbase
{
enum MysqlSetOptEnum {
  MYSQL_OPTION_INVALID = -1,
  MYSQL_OPTION_MULTI_STATEMENTS_ON = 0,
  MYSQL_OPTION_MULTI_STATEMENTS_OFF = 1
};
  
namespace observer
{
class ObMPSetOption
    : public ObMPBase
{
public:
  static const obmysql::ObMySQLCmd COM = obmysql::COM_SET_OPTION;

public:
  explicit ObMPSetOption(const ObGlobalContext &gctx)
      : ObMPBase(gctx),
        set_opt_(MysqlSetOptEnum::MYSQL_OPTION_INVALID)
  {}
  virtual ~ObMPSetOption() {}

  int deserialize();

protected:
  int process();

  uint16_t set_opt_;
}; // end of class ObMPStatistic
} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OBMP_SET_OPTION_H_ */
