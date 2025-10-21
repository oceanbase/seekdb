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

#include "lib/allocator/ob_allocator.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
} // namespace schema
} // namespace share
namespace sql
{
class ObExecContext;
class ObSQLSessionInfo;
} // namespace sql
namespace transaction
{
class ObTxDesc;
} // namespace transaction
namespace observer
{

class ObTableLoadExecCtx
{
public:
  ObTableLoadExecCtx() : exec_ctx_(nullptr), tx_desc_(nullptr) {}
  virtual ~ObTableLoadExecCtx() = default;
  common::ObIAllocator *get_allocator();
  sql::ObSQLSessionInfo *get_session_info();
  share::schema::ObSchemaGetterGuard *get_schema_guard();
  virtual int check_status();
  bool is_valid() const { return nullptr != exec_ctx_; }
  TO_STRING_KV(KP_(exec_ctx), KP_(tx_desc));
public:
  sql::ObExecContext *exec_ctx_;
  transaction::ObTxDesc *tx_desc_;
};

class ObTableLoadClientExecCtx : public ObTableLoadExecCtx
{
public:
  ObTableLoadClientExecCtx()
    : heartbeat_timeout_us_(0),
      last_heartbeat_time_(0)
  {
  }
  virtual ~ObTableLoadClientExecCtx() = default;
  virtual int check_status();
  void init_heart_beat(const int64_t heartbeat_timeout_us);
  void heart_beat();
  TO_STRING_KV(KP_(exec_ctx), KP_(tx_desc), K_(heartbeat_timeout_us), K_(last_heartbeat_time));
private:
  int64_t heartbeat_timeout_us_;
  int64_t last_heartbeat_time_;
};

} // namespace observer
} // namespace oceanbase
