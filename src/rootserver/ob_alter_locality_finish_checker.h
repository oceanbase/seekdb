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

#ifndef OCEANBASE_ROOTSERVER_OB_ALTER_LOCALITY_FINISH_CHECKER_
#define OCEANBASE_ROOTSERVER_OB_ALTER_LOCALITY_FINISH_CHECKER_

#include "share/ob_define.h"
#include "ob_root_utils.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
}
class ObLSTableOperator;
}
namespace rootserver
{
class ObUnitManager;
class ObZoneManager;
class DRLSInfo;
class LocalityMap;

struct ObCommitAlterTenantLocalityArg : public obrpc::ObDDLArg
{
  OB_UNIS_VERSION(1);
public:
  ObCommitAlterTenantLocalityArg() : tenant_id_(common::OB_INVALID_ID) {}
  bool is_valid() const { return common::OB_INVALID_ID != tenant_id_;}
  TO_STRING_KV(K_(tenant_id));

  uint64_t tenant_id_;
};

class ObAlterLocalityFinishChecker : public share::ObCheckStopProvider
{
public:
  ObAlterLocalityFinishChecker(volatile bool &stop);
  virtual ~ObAlterLocalityFinishChecker();
public:
  int init(
      share::schema::ObMultiVersionSchemaService &schema_service,
      obrpc::ObCommonRpcProxy &common_rpc_proxy,
      common::ObAddr &self,
      common::ObMySQLProxy &sql_proxy,
      share::ObLSTableOperator &lst_operator);
  int check();
  static int find_rs_job(const uint64_t tenant_id, int64_t &job_id, ObISQLClient &sql_proxy);

private:
  //check whether this checker is stopped
  virtual int check_stop() const override;
  virtual int check_tenant_previous_locality_(const uint64_t tenant_id, bool &is_previous_locality_empty);
private:
  bool inited_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  obrpc::ObCommonRpcProxy *common_rpc_proxy_;   //use GCTX.rs_rpc_proxy_
  common::ObAddr self_;
  common::ObMySQLProxy *sql_proxy_;
  share::ObLSTableOperator *lst_operator_;
  volatile bool &stop_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterLocalityFinishChecker);
};
} // end namespace rootserver
} // end namespace oceanbase
#endif // OCEANBASE_ROOTSERVER_OB_ALTER_LOCALITY_FINISH_CHECKER_
