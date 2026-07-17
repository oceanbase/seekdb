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

#ifndef OCEANBASE_TRANSACTION_OB_TRANS_CTX_MGR_
#define OCEANBASE_TRANSACTION_OB_TRANS_CTX_MGR_
#include "lib/ob_define.h"
#include "lib/lock/ob_spin_rwlock.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "lib/lock/ob_bucket_lock.h"
#include "lib/hash/ob_linear_hash_map.h"
#include "lib/hash/ob_link_hashmap.h"
#include "lib/container/ob_se_array.h"
#include "common/ob_simple_iterator.h"
#include "share/ob_ls_id.h"
#include "share/ob_light_hashmap.h"
#include "storage/memtable/ob_memtable_context.h"
#include "ob_trans_ctx.h"
#include "ob_trans_stat.h"

#include "storage/tx/ob_tx_ls_log_writer.h"
#include "storage/tx_table/ob_tx_table_define.h"
#include "storage/tx/ob_tx_log_adapter.h"
#include "ob_trans_ctx_mgr_v4.h"

namespace oceanbase
{

namespace transaction
{

struct ObELRStatSummary {
  ObELRStatSummary() { reset(); }

  void reset()
  {
    with_dependency_trx_count_ = 0;
    without_dependency_trx_count_ = 0;
    end_trans_by_prev_count_ = 0;
    end_trans_by_checkpoint_count_ = 0;
    end_trans_by_self_count_ = 0;
  }

  TO_STRING_KV(K_(with_dependency_trx_count), K_(without_dependency_trx_count),
      K_(end_trans_by_prev_count), K_(end_trans_by_checkpoint_count), K_(end_trans_by_self_count));

  uint64_t with_dependency_trx_count_;
  uint64_t without_dependency_trx_count_;
  uint64_t end_trans_by_prev_count_;
  uint64_t end_trans_by_checkpoint_count_;
  uint64_t end_trans_by_self_count_;
};

}
}
#endif // OCEANBASE_TRANSACTION_OB_TRANS_CTX_MGR_
