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
#ifndef OCEANBASE_ROOTSERVER_OB_CREATE_VIEW_HELPER_H_
#define OCEANBASE_ROOTSERVER_OB_CREATE_VIEW_HELPER_H_

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
class ObCreateTableArg;
class ObCreateTableRes;
}
namespace rootserver
{
class ObCreateViewHelper : public ObDDLHelper
{
public:
  ObCreateViewHelper(
    share::schema::ObMultiVersionSchemaService *schema_service,
    const uint64_t tenant_id,
    const obrpc::ObCreateTableArg &arg,
    obrpc::ObCreateTableRes &res);
  virtual ~ObCreateViewHelper();

private:
  int lock_objects_();
  int generate_schemas_();
  virtual int calc_schema_version_cnt_() override;
  virtual int init_() override { return OB_SUCCESS; }
  virtual int operate_schemas_() override { return OB_SUCCESS; }
  virtual int operation_before_commit_() override { return OB_SUCCESS; }
  virtual int clean_on_fail_commit_() override { return OB_SUCCESS; }
  virtual int construct_and_adjust_result_(int &return_ret) override { return OB_SUCCESS; }
private:
  const obrpc::ObCreateTableArg &arg_;
  obrpc::ObCreateTableRes &res_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateViewHelper);
};

} // end namespace rootserver
} // end namespace oceanbase


#endif//OCEANBASE_ROOTSERVER_OB_CREATE_VIEW_HELPER_H_
