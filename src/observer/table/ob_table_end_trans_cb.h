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

#ifndef _OB_TABLE_END_TRANS_CB_H
#define _OB_TABLE_END_TRANS_CB_H 1
#include "ob_table_rpc_response_sender.h"
#include "sql/ob_end_trans_callback.h"
#include "share/table/ob_table.h"
#include "ob_htable_lock_mgr.h"
#include "share/table/ob_table_rpc_struct.h"
#include "observer/table/group/ob_table_group_common.h"

namespace oceanbase
{
namespace table
{
class ObTableAPITransCb;
class ObTableCreateCbFunctor
{
public:
  ObTableCreateCbFunctor()
      : is_inited_(false)
  {}
  TO_STRING_KV(K_(is_inited));
  virtual ~ObTableCreateCbFunctor() = default;
public:
  virtual ObTableAPITransCb* new_callback() = 0;
  virtual ObTableAPITransCb* get_callback() { return nullptr; }
protected:
  bool is_inited_;
};

class ObTableExecuteCreateCbFunctor : public ObTableCreateCbFunctor
{
public:
  ObTableExecuteCreateCbFunctor()
      : req_(nullptr),
        result_(nullptr),
        op_type_(ObTableOperationType::Type::INVALID)
  {}
  virtual ~ObTableExecuteCreateCbFunctor() = default;
public:
  int init(rpc::ObRequest *req, const ObTableOperationResult *result, ObTableOperationType::Type op_type);
  virtual ObTableAPITransCb* new_callback() override;
private:
  rpc::ObRequest *req_;
  const ObTableOperationResult *result_;
  ObTableOperationType::Type op_type_;
};

class ObTableBatchExecuteCreateCbFunctor : public ObTableCreateCbFunctor
{
public:
  ObTableBatchExecuteCreateCbFunctor()
      : req_(nullptr),
        result_(nullptr),
        op_type_(ObTableOperationType::Type::INVALID)
  {}
  virtual ~ObTableBatchExecuteCreateCbFunctor() = default;
public:
  int init(rpc::ObRequest *req, const ObTableBatchOperationResult *result, ObTableOperationType::Type op_type);
  virtual ObTableAPITransCb* new_callback() override;
private:
  rpc::ObRequest *req_;
  const ObTableBatchOperationResult *result_;
  ObTableOperationType::Type op_type_;
};

class ObTableLSExecuteEndTransCb;
class ObTableLSExecuteCreateCbFunctor : public ObTableCreateCbFunctor
{
public:
  ObTableLSExecuteCreateCbFunctor()
      : req_(nullptr),
        cb_(nullptr)
  {}
  virtual ~ObTableLSExecuteCreateCbFunctor() = default;
  void reset()
  {
    req_ = nullptr;
    cb_ = nullptr;
    is_inited_ = false;
  }
public:
  int init(rpc::ObRequest *req);
  virtual ObTableAPITransCb* new_callback() override;
  virtual ObTableAPITransCb* get_callback() override { return cb_; }
private:
  rpc::ObRequest *req_;
  ObTableAPITransCb *cb_;
};

class ObTableAPITransCb: public sql::ObExclusiveEndTransCallback
{
public:
  ObTableAPITransCb();
  virtual ~ObTableAPITransCb();
  void destroy_cb_if_no_ref();
  void destroy_cb();
  void set_tx_desc(transaction::ObTxDesc *tx_desc) { tx_desc_ = tx_desc; }
  void set_lock_handle(ObHTableLockHandle *lock_handle);
protected:
  void check_callback_timeout();
protected:
  int64_t create_ts_;
  ObCurTraceId::TraceId trace_id_;
  transaction::ObTxDesc *tx_desc_;
  ObHTableLockHandle *lock_handle_; // hbase row lock handle
private:
  int32_t ref_count_;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObTableAPITransCb);
};

class ObTableExecuteEndTransCb: public ObTableAPITransCb
{
public:
  ObTableExecuteEndTransCb(rpc::ObRequest *req, ObTableOperationType::Type table_operation_type)
    : allocator_(ObMemAttr(MTL_ID(), "TabelExeCbAlloc")),
      response_sender_(req, &result_)
  {
    result_.set_type(table_operation_type);
  }
  virtual ~ObTableExecuteEndTransCb() = default;

  virtual void callback(int cb_param) override;
  virtual void callback(int cb_param, const transaction::ObTransID &trans_id) override;
  virtual const char *get_type() const override { return "ObTableEndTransCallback"; }
  virtual sql::ObEndTransCallbackType get_callback_type() const override { return sql::ASYNC_CALLBACK_TYPE; }
  int assign_execute_result(const ObTableOperationResult &result);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObTableExecuteEndTransCb);
private:
  ObTableEntity result_entity_;
  common::ObArenaAllocator allocator_;
  ObTableOperationResult result_;
  obrpc::ObTableRpcResponseSender response_sender_;
};

class ObTableBatchExecuteEndTransCb: public ObTableAPITransCb
{
public:
  ObTableBatchExecuteEndTransCb(rpc::ObRequest *req, ObTableOperationType::Type table_operation_type)
    : allocator_(ObMemAttr(MTL_ID(), "TableBatCbAlloc")),
      entity_factory_("TableBatchCbEntFac", MTL_ID()),
      response_sender_(req, &result_),
      table_operation_type_(table_operation_type)
  {
  }
  virtual ~ObTableBatchExecuteEndTransCb() = default;

  virtual void callback(int cb_param) override;
  virtual void callback(int cb_param, const transaction::ObTransID &trans_id) override;
  virtual const char *get_type() const override { return "ObTableBatchEndTransCallback"; }
  virtual sql::ObEndTransCallbackType get_callback_type() const override { return sql::ASYNC_CALLBACK_TYPE; }
  int assign_batch_execute_result(const ObTableBatchOperationResult &result);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObTableBatchExecuteEndTransCb);
private:
  ObTableEntity result_entity_;
  common::ObArenaAllocator allocator_;
  ObTableEntityFactory<ObTableEntity> entity_factory_;
  ObTableBatchOperationResult result_;
  obrpc::ObTableRpcResponseSender response_sender_;
  ObTableOperationType::Type table_operation_type_;
};

class ObTableLSExecuteEndTransCb: public ObTableAPITransCb
{
public:
  ObTableLSExecuteEndTransCb(rpc::ObRequest *req)
    : allocator_(ObMemAttr(MTL_ID(), "TableLSCbAlloc")),
      entity_factory_("TableLSCbEntFac", MTL_ID()),
      response_sender_(req, &result_)
  {
    dependent_results_.set_attr(ObMemAttr(MTL_ID(), "LsCbDepRes"));
    is_alloc_from_pool_ = true;
  }
  virtual ~ObTableLSExecuteEndTransCb() = default;

  virtual void callback(int cb_param) override;
  virtual void callback(int cb_param, const transaction::ObTransID &trans_id) override;
  virtual const char *get_type() const override { return "ObTableLSEndTransCallback"; }
  virtual sql::ObEndTransCallbackType get_callback_type() const override { return sql::ASYNC_CALLBACK_TYPE; }
  OB_INLINE ObTableLSOpResult &get_result() { return result_; }
  OB_INLINE ObTableEntityFactory<ObTableSingleOpEntity> &get_entity_factory() { return entity_factory_; }
  OB_INLINE ObIAllocator &get_allocator() { return allocator_; }
  OB_INLINE int assign_dependent_results(common::ObIArray<ObTableLSOpResult*> &results, bool is_alloc_from_pool)
  {
    is_alloc_from_pool_ = is_alloc_from_pool;
    return dependent_results_.assign(results);
  }
  void free_dependent_results();
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObTableLSExecuteEndTransCb);
private:
  common::ObArenaAllocator allocator_;
  ObTableSingleOpEntity result_entity_;
  ObTableEntityFactory<ObTableSingleOpEntity> entity_factory_;
  ObTableLSOpResult result_;
  common::ObSEArray<ObTableLSOpResult*, 3> dependent_results_;
  obrpc::ObTableRpcResponseSender response_sender_;
  bool is_alloc_from_pool_;
};

} // end namespace table
} // end namespace oceanbase

#endif /* _OB_TABLE_END_TRANS_CB_H */
