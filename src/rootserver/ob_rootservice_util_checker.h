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

#ifndef OCEANBASE_ROOTSERVICE_UTIL_CHECKER_H_
#define OCEANBASE_ROOTSERVICE_UTIL_CHECKER_H_ 1
#include "lib/container/ob_array.h"
#include "lib/net/ob_addr.h"
#include "ob_migrate_unit_finish_checker.h"
#include "ob_alter_locality_finish_checker.h"
#include "ob_shrink_expand_resource_pool_checker.h"

namespace oceanbase
{
namespace rootserver
{
class ObRootServiceUtilChecker : public share::ObCheckStopProvider
{
public:
  ObRootServiceUtilChecker(volatile bool &stop);
  virtual ~ObRootServiceUtilChecker();
public:
  int init(
      ObUnitManager &unit_mgr,
      obrpc::ObCommonRpcProxy &common_rpc_proxy,
      common::ObAddr &self,
      share::schema::ObMultiVersionSchemaService &schema_service,
      common::ObMySQLProxy &sql_proxy,
      share::ObLSTableOperator &lst_operator);
public:
  int rootservice_util_check();
private:
  virtual int check_stop() const override;
private:
  bool inited_;
  volatile bool &stop_;
  ObMigrateUnitFinishChecker migrate_unit_finish_checker_;
  ObAlterLocalityFinishChecker alter_locality_finish_checker_;
  ObShrinkExpandResourcePoolChecker shrink_expand_resource_pool_checker_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObRootServiceUtilChecker);
};

} // end namespace rootserver
} // end namespace oceanbase

#endif /* OCEANBASE_ROOTSERVICE_UTIL_CHECKER_H_ */
