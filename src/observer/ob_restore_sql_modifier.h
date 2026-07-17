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

#ifndef _OB_RS_RESTORE_SQL_MODIFIER_H_
#define _OB_RS_RESTORE_SQL_MODIFIER_H_

namespace oceanbase
{

namespace sql
{
class ObResultSet;
}

namespace observer
{
class ObRestoreSQLModifier
{
public:
  virtual int modify(sql::ObResultSet &rs) = 0;
};
}

}
#endif /* _OB_RS_RESTORE_SQL_MODIFIER_H_ */
//// end of header file
