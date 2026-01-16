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

#pragma once

#include "observer/table_load/ob_table_load_trans_ctx.h"
#include "observer/table_load/ob_table_load_struct.h"
#include "share/table/ob_table_load_define.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadTransStore;
class ObTableLoadTransStoreWriter;

struct ObTableLoadStoreTrans
{
  ObTableLoadStoreTrans(ObTableLoadTransCtx *trans_ctx);
  ~ObTableLoadStoreTrans();
  int init();
  OB_INLINE ObTableLoadTransCtx *get_trans_ctx() const { return trans_ctx_; }
  OB_INLINE const table::ObTableLoadTransId &get_trans_id() const
  {
    return trans_ctx_->trans_id_;
  }
  ObTableLoadTransStoreWriter *get_store_writer() const { return trans_store_writer_; }
  ObTableLoadTransStore *get_trans_store() { return trans_store_; }
  int output_store(ObTableLoadTransStore *&trans_store);
  int64_t get_ref_count() const { return ATOMIC_LOAD(&ref_count_); }
  int64_t inc_ref_count() { return ATOMIC_AAF(&ref_count_, 1); }
  int64_t dec_ref_count() { return ATOMIC_AAF(&ref_count_, -1); }
  bool is_dirty() const { return is_dirty_; }
  void set_dirty() { is_dirty_ = true; }
  TO_STRING_KV(KP_(trans_ctx), K_(is_dirty));
public:
  OB_INLINE int check_trans_status(table::ObTableLoadTransStatusType trans_status) const
  {
    return trans_ctx_->check_trans_status(trans_status);
  }
  OB_INLINE int check_trans_status(table::ObTableLoadTransStatusType trans_status1,
                                   table::ObTableLoadTransStatusType trans_status2) const
  {
    return trans_ctx_->check_trans_status(trans_status1, trans_status2);
  }
  OB_INLINE int set_trans_status_inited()
  {
    return advance_trans_status(table::ObTableLoadTransStatusType::INITED);
  }
  OB_INLINE int set_trans_status_running()
  {
    return advance_trans_status(table::ObTableLoadTransStatusType::RUNNING);
  }
  OB_INLINE int set_trans_status_frozen()
  {
    return advance_trans_status(table::ObTableLoadTransStatusType::FROZEN);
  }
  OB_INLINE int set_trans_status_commit()
  {
    return advance_trans_status(table::ObTableLoadTransStatusType::COMMIT);
  }
  int set_trans_status_error(int error_code);
  int set_trans_status_abort();
private:
  int advance_trans_status(table::ObTableLoadTransStatusType trans_status);
private:
  ObTableLoadTransCtx * const trans_ctx_;
  ObTableLoadTransStore *trans_store_;
  ObTableLoadTransStoreWriter *trans_store_writer_;
  int64_t ref_count_ CACHE_ALIGNED;
  volatile bool is_dirty_;
  bool is_inited_;
};

}  // namespace observer
}  // namespace oceanbase
