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

#ifndef OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_TRUSTED_CERTIFICATE_MANAGER_PL_H_
#define OCEANBASE_SRC_PL_SYS_PACKAGE_DBMS_TRUSTED_CERTIFICATE_MANAGER_PL_H_

#include "sql/engine/ob_exec_context.h"

namespace oceanbase 
{
namespace pl
{

#define INSERT_ALL_TRUSTED_ROOT_CERTIFICAT_SQL " \
  insert into %s                       \
  (common_name, description, content)  \
  values ('%.*s', '%.*s', '%.*s')      \
  "

#define DELETE_ALL_TRUSTED_ROOT_CERTIFICAT_SQL " \
  delete from %s             \
  where common_name='%.*s'   \
  "
#define UPDATE_ALL_TRUSTED_ROOT_CERTIFICAT_SQL "     \
  update %s set description='%.*s', content='%.*s'   \
  where common_name='%.*s'                           \
  "

class ObPlDBMSTrustedCertificateManager
{
public:
  ObPlDBMSTrustedCertificateManager() {}
  virtual ~ObPlDBMSTrustedCertificateManager() {}
public:
  static int add_trusted_certificate(
          sql::ObExecContext &ctx,
          sql::ParamStore &params,
          common::ObObj &result);
  static int delete_trusted_certificate(
          sql::ObExecContext &ctx,
          sql::ParamStore &params,
          common::ObObj &result);
  static int update_trusted_certificate(
          sql::ObExecContext &ctx,
          sql::ParamStore &params,
          common::ObObj &result);
private:
  static int check_data_version_and_privilege(sql::ObExecContext &ctx);
  DISALLOW_COPY_AND_ASSIGN(ObPlDBMSTrustedCertificateManager);
};

}
}

#endif
