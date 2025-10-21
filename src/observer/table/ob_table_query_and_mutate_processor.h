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

#ifndef _OB_TABLE_QUERY_AND_MUTATE_PROCESSOR_H
#define _OB_TABLE_QUERY_AND_MUTATE_PROCESSOR_H 1
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_processor.h"
#include "share/table/ob_table_rpc_proxy.h"
#include "ob_table_rpc_processor.h"
#include "ob_table_context.h"
#include "ob_table_scan_executor.h"
#include "ob_table_update_executor.h"
#include "ob_table_insert_executor.h"
#include "ob_table_cache.h"
#include "ob_table_op_wrapper.h"
#include "ob_table_query_common.h"


namespace oceanbase
{
namespace observer
{
class ObTableQueryAndMutateP: public ObTableRpcProcessor<obrpc::ObTableRpcProxy::ObRpc<obrpc::OB_TABLE_API_QUERY_AND_MUTATE> >
{
  typedef ObTableRpcProcessor<obrpc::ObTableRpcProxy::ObRpc<obrpc::OB_TABLE_API_QUERY_AND_MUTATE> > ParentType;
public:
  explicit ObTableQueryAndMutateP(const ObGlobalContext &gctx);
  virtual ~ObTableQueryAndMutateP() {}

  virtual int deserialize() override;
  virtual int before_process() override;
protected:
  virtual int check_arg() override;
  virtual int try_process() override;
  virtual void reset_ctx() override;
  virtual uint64_t get_request_checksum() override;
  virtual table::ObTableEntityType get_entity_type() override { return arg_.entity_type_; }
  virtual bool is_kv_processor() override { return true; }
private:
  int32_t get_stat_process_type(bool is_hkv, bool is_check_and_execute, table::ObTableOperationType::Type type);
  int old_try_process();
  int new_try_process();
  virtual bool is_new_try_process() override;
private:
  table::ObTableCtx tb_ctx_;
  table::ObTableEntityFactory<table::ObTableEntity> default_entity_factory_;

  table::ObTableQueryResult one_result_;
  bool end_in_advance_; // flag for early termination
  DISALLOW_COPY_AND_ASSIGN(ObTableQueryAndMutateP);
};
} // end namespace observer
} // end namespace oceanbase

#endif /* _OB_TABLE_QUERY_AND_MUTATE_PROCESSOR_H */
