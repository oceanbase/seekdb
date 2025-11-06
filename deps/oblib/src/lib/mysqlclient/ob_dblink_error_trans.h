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
 
#ifndef OBDBLINKERROR_H
#define OBDBLINKERROR_H
#include "lib/utility/ob_edit_distance.h"
#include "lib/ob_errno.h"
#include "lib/mysqlclient/ob_isql_connection.h"

extern bool get_dblink_reuse_connection_cfg();

namespace oceanbase
{
namespace common
{
namespace sqlclient
{

#define TRANSLATE_CLIENT_ERR(ret, errmsg)  \
  const int orginal_ret = ret;\
  bool is_oracle_err = lib::is_oracle_mode();\
  int translate_ret = OB_SUCCESS;\
  if (OB_SUCCESS == ret) {\
  } else if (OB_SUCCESS != (translate_ret = oceanbase::common::sqlclient::ObDblinkErrorTrans::\
      external_errno_to_ob_errno(is_oracle_err, orginal_ret, errmsg, ret))) {\
    LOG_WARN("failed to translate client error code", K(translate_ret), K(orginal_ret), K(ret), K(is_oracle_err), K(errmsg));\
  } else {\
    LOG_WARN("succ to translate client error code", K(translate_ret), K(orginal_ret), K(ret), K(is_oracle_err), K(errmsg));\
  }

#define TRANSLATE_CLIENT_ERR_2(ret, is_oracle_err, errmsg)  \
  const int orginal_ret = ret;\
  int translate_ret = OB_SUCCESS;\
  if (OB_SUCCESS == ret) {\
  } else if (OB_SUCCESS != (translate_ret = oceanbase::common::sqlclient::ObDblinkErrorTrans::\
      external_errno_to_ob_errno(is_oracle_err, orginal_ret, errmsg, ret))) {\
    LOG_WARN("failed to translate client error code", K(translate_ret), K(orginal_ret), K(ret), K(is_oracle_err), K(errmsg));\
  } else {\
    LOG_WARN("succ to translate client error code", K(translate_ret), K(orginal_ret), K(ret), K(is_oracle_err), K(errmsg));\
  }

class ObDblinkErrorTrans {
public:
  static int external_errno_to_ob_errno(bool is_oci_client, 
                                        int external_errno, 
                                        const char *external_errmsg, 
                                        int &ob_errno);
};

} // namespace sqlclient
} // namespace common
} // namespace oceanbase
#endif //OBDBLINKERROR_H
