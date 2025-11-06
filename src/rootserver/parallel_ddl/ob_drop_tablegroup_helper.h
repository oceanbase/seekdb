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
#ifndef OCEANBASE_ROOTSERVER_OB_DROP_TABLEGROUP_H_
#define OCEANBASE_ROOTSERVER_OB_DROP_TABLEGROUP_H_

#include "rootserver/parallel_ddl/ob_ddl_helper.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
}
}
namespace obrpc
{
class ObDropTablegroupArg;
}
namespace rootserver
{
class ObDropTablegroupHelper : public ObDDLHelper
{
public:
  ObDropTablegroupHelper(
    share::schema::ObMultiVersionSchemaService *schema_service,
    const uint64_t tenant_id,
    const obrpc::ObDropTablegroupArg &arg,
    obrpc::ObParallelDDLRes &res,
    ObDDLSQLTransaction *external_trans = nullptr);
  virtual ~ObDropTablegroupHelper();
private:
  virtual int check_inner_stat_() override;
  virtual int init_();
  virtual int lock_objects_() override;
  virtual int generate_schemas_() override;
  virtual int calc_schema_version_cnt_() override;
  virtual int operate_schemas_() override;
  virtual int operation_before_commit_() override;
  virtual int clean_on_fail_commit_() override;
  virtual int construct_and_adjust_result_(int &return_ret) override;
private:
  int lock_tablegroup_by_obj_id_();
private:
  const obrpc::ObDropTablegroupArg &arg_;
  obrpc::ObParallelDDLRes &res_;
  const ObTablegroupSchema* tablegroup_schema_;
  uint64_t tablegroup_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropTablegroupHelper);
};

} // end namespace rootserver
} // end namespace oceanbase
 
#endif//OCEANBASE_ROOTSERVER_OB_DROP_TABLEGROUP_H_
