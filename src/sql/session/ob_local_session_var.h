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

#ifndef OCEANBASE_SQL_LOCAL_SESSION_VAR_H_
#define OCEANBASE_SQL_LOCAL_SESSION_VAR_H_

#include "share/session/ob_local_session_var.h"

namespace oceanbase
{
namespace sql
{
class ObBasicSessionInfo;

using share::ObLocalSessionVar;
using share::ObSessionSysVar;

class ObLocalSessionVarHelper
{
public:
  static int load_session_vars(const ObBasicSessionInfo *session, ObLocalSessionVar &local_vars);
  static int reserve_max_local_vars_capacity(ObLocalSessionVar &local_vars);
  static int update_session_vars_with_local(const ObLocalSessionVar &local_vars,
                                            ObBasicSessionInfo &session);
  static int remove_vars_same_with_session(ObLocalSessionVar &local_vars,
                                           const ObBasicSessionInfo *session);
  static int get_different_vars_from_session(const ObLocalSessionVar &local_vars,
                                             const ObBasicSessionInfo *session,
                                             common::ObIArray<const ObSessionSysVar*> &local_diff_vars,
                                             common::ObIArray<common::ObObj> &session_vals);
  static int check_var_same_with_session(const ObBasicSessionInfo &session,
                                         const ObSessionSysVar *local_var,
                                         bool &is_same,
                                         common::ObObj *diff_val = NULL);
  static int get_sys_var_val_str(const share::ObSysVarClassType var_type,
                                 const common::ObObj &var_val,
                                 common::ObIAllocator &allocator,
                                 common::ObString &val_str);

private:
  static const share::ObSysVarClassType ALL_LOCAL_VARS[];
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_LOCAL_SESSION_VAR_H_ */
