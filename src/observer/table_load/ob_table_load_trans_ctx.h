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

#include "lib/allocator/page_arena.h"
#include "lib/utility/ob_print_utils.h"
#include "share/table/ob_table_load_define.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadTableCtx;

struct ObTableLoadTransCtx
{
public:
  enum TransType
  {
    INVALID_TYPE = 0,
    COORDINATOR = 1,
    STORE = 2,
    MAX_TYPE
  };
  static bool is_trans_type_valid(const TransType trans_type)
  {
    return trans_type > TransType::INVALID_TYPE && trans_type < TransType::MAX_TYPE;
  }
public:
  ObTableLoadTransCtx(ObTableLoadTableCtx *ctx, const TransType trans_type, const table::ObTableLoadTransId &trans_id);
  OB_INLINE table::ObTableLoadTransStatusType get_trans_status() const
  {
    obsys::ObRLockGuard guard(rwlock_);
    return trans_status_;
  }
  OB_INLINE void get_trans_status(table::ObTableLoadTransStatusType &trans_status,
                                  int &error_code) const
  {
    obsys::ObRLockGuard guard(rwlock_);
    trans_status = trans_status_;
    error_code = error_code_;
  }
  int advance_trans_status(table::ObTableLoadTransStatusType trans_status);
  int set_trans_status_error(int error_code);
  int set_trans_status_abort(int error_code = OB_CANCELED);
  int check_trans_status(table::ObTableLoadTransStatusType trans_status) const;
  int check_trans_status(table::ObTableLoadTransStatusType trans_status1,
                         table::ObTableLoadTransStatusType trans_status2) const;
  TO_STRING_KV(K_(trans_type), K_(trans_id), K_(trans_status), K_(error_code));
public:
  ObTableLoadTableCtx * const ctx_;
  const TransType trans_type_;
  const table::ObTableLoadTransId trans_id_;
  mutable obsys::ObRWLock rwlock_;
  common::ObArenaAllocator allocator_;
  table::ObTableLoadTransStatusType trans_status_;
  int error_code_;
};

}  // namespace observer
}  // namespace oceanbase
