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

#define USING_LOG_PREFIX LIB_MYSQLC
#include "lib/mysqlclient/ob_dblink_error_trans.h"

int __attribute__((weak)) get_oracle_errno(int index)
{
  return oceanbase::OB_SUCCESS;
}

int __attribute__((weak)) get_mysql_errno(int index)
{
  return oceanbase::OB_SUCCESS;
}



bool __attribute__((weak)) get_dblink_reuse_connection_cfg()
{
  return true;
}




namespace oceanbase
{
namespace common
{
namespace sqlclient
{

int sqlclient::ObDblinkErrorTrans::external_errno_to_ob_errno(bool is_oracle_err, 
                                                   int external_errno, 
                                                   const char *external_errmsg, 
                                                   int &ob_errno) {
  int ret = OB_SUCCESS;
  external_errno = abs(external_errno);
  if (OB_SUCCESS != external_errno) {
    const char *oracle_msg_prefix = "ORA";
    if (external_errno >= 2000 && // google "Client Error Message Reference" 
        external_errno <= 2075 && // you will known errno in [2000, 2075] is client error at dev.mysql.com
        (!is_oracle_err ||  
        (is_oracle_err && 
        (OB_NOT_NULL(external_errmsg) && 0 != STRLEN(external_errmsg)) && 
        0 != std::memcmp(oracle_msg_prefix, external_errmsg, 
        std::min(STRLEN(oracle_msg_prefix), STRLEN(external_errmsg)))))) {
      ob_errno = external_errno; // do not map, show user client errno directly.
    } else if (is_oracle_err
               && -external_errno >= OB_MIN_RAISE_APPLICATION_ERROR
               && -external_errno <= OB_MAX_RAISE_APPLICATION_ERROR) {
      ob_errno = OB_APPLICATION_ERROR_FROM_REMOTE;
      LOG_USER_ERROR(OB_APPLICATION_ERROR_FROM_REMOTE, (int)STRLEN(external_errmsg), external_errmsg);
    } else {
      int64_t match_count = 0;
      for (int i = 0; i < oceanbase::common::OB_MAX_ERROR_CODE; ++i) {
        if (external_errno == (is_oracle_err ? get_oracle_errno(i) : get_mysql_errno(i))) {
          ob_errno = -i;
          ++match_count;
        }
      }
      if (1 != match_count) {
        // default ob_errno, if external_errno can not map to any valid ob_errno
        ob_errno = OB_ERR_DBLINK_REMOTE_ECODE;
	const char *errmsg = external_errmsg;
	if (NULL == errmsg) {
		errmsg = "empty error message";
	}
        int msg_len = STRLEN(errmsg);
        LOG_USER_ERROR(OB_ERR_DBLINK_REMOTE_ECODE, external_errno, msg_len, errmsg);
      } else if (1 == match_count) {
        if (is_oracle_err && OB_TRANS_XA_BRANCH_FAIL == ob_errno) {
          ob_errno = OB_TRANS_NEED_ROLLBACK;
        }
      }
    }
  }
  return ret;
}

} // end namespace sqlclient
} // end namespace common
} // end namespace oceanbase
