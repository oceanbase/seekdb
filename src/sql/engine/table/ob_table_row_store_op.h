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

#ifndef SQL_ENGINE_BASIC_OB_TABLE_ROW_STORE_
#define SQL_ENGINE_BASIC_OB_TABLE_ROW_STORE_
#include "lib/container/ob_array_wrap.h"
#include "common/row/ob_row_store.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_operator.h"

namespace oceanbase
{
namespace sql
{
class ObTableRowStoreOpInput : public ObOpInput
{
  friend class ObTableRowStoreOp;
  OB_UNIS_VERSION(1);
public:
  ObTableRowStoreOpInput(ObExecContext &ctx, const ObOpSpec &spec)
    : ObOpInput(ctx, spec),
      multi_row_store_(),
      allocator_(NULL)
  { }
  virtual ~ObTableRowStoreOpInput() {}

  virtual void reset() override
  {
    multi_row_store_.reset();
    //deserialize_allocator_ cannot be reset
    //because it is only set once when creating operator input
  }
  virtual int init(ObTaskInfo &task_info) override;
  /**
   * @brief set allocator which is used for deserialize, but not all objects will use allocator
   * while deserializing, so you can override it if you need.
   */
  virtual void set_deserialize_allocator(common::ObIAllocator *allocator);
private:
  // One partition corresponds to one row store
  common::ObFixedArray<ObChunkDatumStore *, common::ObIAllocator> multi_row_store_;
  common::ObIAllocator *allocator_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTableRowStoreOpInput);
};

class ObTableRowStoreSpec : public ObOpSpec
{
  OB_UNIS_VERSION(1);
public:
  ObTableRowStoreSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
      table_id_(common::OB_INVALID_ID)
  {}
  ~ObTableRowStoreSpec() {}
public:
  uint64_t table_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTableRowStoreSpec);
};

class ObTableRowStoreOp : public ObOperator
{
public:
  ObTableRowStoreOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(exec_ctx, spec, input),
      row_store_idx_(0)
  {}

  virtual ~ObTableRowStoreOp() { destroy(); }

  virtual void destroy() { ObOperator::destroy(); }
  int inner_rescan();
  /**
   * @brief called by get_next_row(), get a row from the child operator or row_store
   * @param ctx[in], execute context
   * @param row[out], ObSqlRow an obj array and row_size
   */
  virtual int inner_get_next_row();
  /**
   * @brief open operator, not including children operators.
   * called by open.
   * Every op should implement this method.
   */
  virtual int inner_open();
  /**
   * @brief close operator, not including children operators.
   * Every op should implement this method.
   */
  virtual int inner_close();
  virtual int64_t to_string_kv(char *buf, const int64_t buf_len);

  int fetch_stored_row();
private:
  ObChunkDatumStore::Iterator row_store_it_;
  int64_t row_store_idx_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObTableRowStoreOp);
};
}  // namespace sql
}  // namespace oceanbase
#endif /* SQL_ENGINE_BASIC_OB_TABLE_ROW_STORE_*/
