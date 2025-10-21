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

#define USING_LOG_PREFIX PL
#include "ob_pl_dbms_trusted_certificate_manager.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::pl;
using namespace oceanbase::sql;

int ObPlDBMSTrustedCertificateManager::check_data_version_and_privilege(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = ctx.get_my_session();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else if (!is_sys_tenant(session->get_effective_tenant_id())) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("only sys tenant can operate", K(ret));
  }
  return ret;
}

int ObPlDBMSTrustedCertificateManager::add_trusted_certificate(
    sql::ObExecContext &ctx,
    sql::ParamStore &params,
    common::ObObj &result)
{
  enum {
    COMMON_NAME = 0,
    DESCRIPTION = 1,
    CONTENT
  };
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_data_version_and_privilege(ctx))) {
    LOG_WARN("check_data_version_and_privilege failed", K(ret));
  } else {
    ObString common_name;
    ObString description;
    ObString content;
    //check cert content validity
    for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); i++) {
      ObObj &obj = params.at(i);
      switch(i) {
        case COMMON_NAME: {
          if (OB_FAIL(obj.get_string(common_name))) {
            LOG_WARN("failed to get content string", K(ret));
          }
          break;
        }
        case DESCRIPTION: {
          if (OB_FAIL(obj.get_string(description))) {
            LOG_WARN("failed to get description string", K(ret));
          }
          break;
        }
        case CONTENT: {
          int64_t cert_expired_time = 0;
          if (OB_FAIL(obj.get_string(content))) {
            LOG_WARN("failed to get cert string", K(ret));
          } else if (OB_FAIL(extract_cert_expired_time(content.ptr(), content.length(),
                    cert_expired_time))) {
            LOG_WARN("failed to extract cert expired time", K(ret));
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, "cert content, please check");
          }
          break;
        }
        default:
          break;
      }
    }
    if (OB_SUCC(ret)) {
      int64_t affected_rows = 0;
      ObSqlString sql;
      ObMySQLProxy *mysql_proxy = GCTX.sql_proxy_;
      if (OB_ISNULL(mysql_proxy)) {
        ret = OB_NOT_INIT;
        LOG_WARN("mysql proxy is not inited", K(ret));
      } else if (OB_FAIL(sql.assign_fmt(INSERT_ALL_TRUSTED_ROOT_CERTIFICAT_SQL,
                          share::OB_ALL_TRUSTED_ROOT_CERTIFICATE_TNAME,
                          common_name.length(), common_name.ptr(),
                          description.length(), description.ptr(),
                          content.length(), content.ptr()))) {
        LOG_WARN("format sql failed", KR(ret), K(sql));
      } else if (OB_FAIL(mysql_proxy->write(OB_SYS_TENANT_ID, sql.ptr(), affected_rows))) {
        LOG_WARN("execute sql fail", KR(ret), K(sql));
      }
    }
  }
  return ret;
}

int ObPlDBMSTrustedCertificateManager::delete_trusted_certificate(
    sql::ObExecContext &ctx,
    sql::ParamStore &params,
    common::ObObj &result)
{
  enum {
    COMMON_NAME = 0,
  };
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_data_version_and_privilege(ctx))) {
    LOG_WARN("check_data_version_and_privilege failed", K(ret));
  } else {
    ObString common_name;
    for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); i++) {
      ObObj &obj = params.at(i);
      switch(i) {
        case COMMON_NAME: {
          if (OB_FAIL(obj.get_string(common_name))) {
            LOG_WARN("failed to get content string", K(ret));
          }
          break;
        }
        default:
          break;
      }
    }
    if (OB_SUCC(ret)) {
      int64_t affected_rows = 0;
      ObSqlString sql;
      ObMySQLProxy *mysql_proxy = GCTX.sql_proxy_;
      if (OB_ISNULL(mysql_proxy)) {
        ret = OB_NOT_INIT;
        LOG_WARN("mysql proxy is not inited", K(ret));
      } else if (OB_FAIL(sql.assign_fmt(DELETE_ALL_TRUSTED_ROOT_CERTIFICAT_SQL,
                          share::OB_ALL_TRUSTED_ROOT_CERTIFICATE_TNAME,
                          common_name.length(), common_name.ptr()))) {
        LOG_WARN("format sql failed", KR(ret), K(sql));
      } else if (OB_FAIL(mysql_proxy->write(OB_SYS_TENANT_ID, sql.ptr(), affected_rows))) {
        LOG_WARN("execute sql fail", KR(ret), K(sql));
      }
    }
  }
  return ret;
}


int ObPlDBMSTrustedCertificateManager::update_trusted_certificate(
    sql::ObExecContext &ctx,
    sql::ParamStore &params,
    common::ObObj &result)
{
  enum {
    COMMON_NAME = 0,
    DESCRIPTION = 1,
    CONTENT
  };
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_data_version_and_privilege(ctx))) {
    LOG_WARN("check_data_version_and_privilege failed", K(ret));
  } else {
    ObString common_name;
    ObString description;
    ObString content;
    //check cert content validity
    for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); i++) {
      ObObj &obj = params.at(i);
      switch(i) {
        case COMMON_NAME: {
          if (OB_FAIL(obj.get_string(common_name))) {
            LOG_WARN("failed to get content string", K(ret));
          }
          break;
        }
        case DESCRIPTION: {
          if (OB_FAIL(obj.get_string(description))) {
            LOG_WARN("failed to get description string", K(ret));
          }
          break;
        }
        case CONTENT: {
          int64_t cert_expired_time = 0;
          if (OB_FAIL(obj.get_string(content))) {
            LOG_WARN("failed to get cert string", K(ret));
          } else if (OB_FAIL(extract_cert_expired_time(content.ptr(), content.length(),
                    cert_expired_time))) {
            LOG_WARN("failed to extract cert expired time", K(ret));
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, "cert content, please check");
          }
          break;
        }
        default:
          break;
      }
    }
    if (OB_SUCC(ret)) {
      int64_t affected_rows = 0;
      ObSqlString sql;
      ObMySQLProxy *mysql_proxy = GCTX.sql_proxy_;
      if (OB_ISNULL(mysql_proxy)) {
        ret = OB_NOT_INIT;
        LOG_WARN("mysql proxy is not inited", K(ret));
      } else if (OB_FAIL(sql.assign_fmt(UPDATE_ALL_TRUSTED_ROOT_CERTIFICAT_SQL,
                          share::OB_ALL_TRUSTED_ROOT_CERTIFICATE_TNAME,
                          description.length(), description.ptr(),
                          content.length(), content.ptr(),
                          common_name.length(), common_name.ptr()))) {
        LOG_WARN("format sql failed", KR(ret), K(sql));
      } else if (OB_FAIL(mysql_proxy->write(OB_SYS_TENANT_ID, sql.ptr(), affected_rows))) {
        LOG_WARN("execute sql fail", KR(ret), K(sql));
      }
    }
  }
  return ret;
}

