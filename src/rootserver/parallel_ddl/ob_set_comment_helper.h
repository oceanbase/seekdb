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
#ifndef OCEANBASE_ROOTSERVER_OB_COMMENT_H_
#define OCEANBASE_ROOTSERVER_OB_COMMENT_H_

#include "rootserver/parallel_ddl/ob_ddl_helper.h"
#include "lib/hash/ob_hashmap.h"

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
class ObSetCommentArg;
class ObSetCommenttRes;
}
namespace rootserver
{
class ObSetCommentHelper : public ObDDLHelper
{
public:
  ObSetCommentHelper(
    share::schema::ObMultiVersionSchemaService *schema_service,
    const uint64_t tenant_id,
    const obrpc::ObSetCommentArg &arg,
    obrpc::ObParallelDDLRes &res);
  virtual ~ObSetCommentHelper();
private:
  virtual int check_inner_stat_() override;
  virtual int lock_objects_() override;
  int check_database_legitimacy_();
  virtual int generate_schemas_() override;
  virtual int calc_schema_version_cnt_() override;
  virtual int operate_schemas_() override;
  int lock_databases_by_obj_name_();
  int lock_objects_by_id_();
  int lock_objects_by_name_();
  int lock_for_common_ddl_();
  int check_table_legitimacy_();
  virtual int init_();
  virtual int construct_and_adjust_result_(int &return_ret) override;
  virtual int operation_before_commit_() override;
  virtual int clean_on_fail_commit_() override;
private:
  const obrpc::ObSetCommentArg &arg_;
  obrpc::ObParallelDDLRes &res_;
  uint64_t database_id_;
  uint64_t table_id_;
  const ObTableSchema* orig_table_schema_;
  ObTableSchema* new_table_schema_;
  common::ObSArray<ObColumnSchemaV2*> new_column_schemas_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSetCommentHelper);

};

} // end namespace rootserver
} // end namespace oceanbase

#endif//OCEANBASE_ROOTSERVER_OB_COMMENT_H_
