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

#ifndef OCEANBASE_STORAGE_TX_STORAGE_OB_LOG_STORAGE_ADAPTER_H_
#define OCEANBASE_STORAGE_TX_STORAGE_OB_LOG_STORAGE_ADAPTER_H_

#include "logservice/ob_i_log_storage.h"

namespace oceanbase
{
namespace storage
{
class ObLSService;
class ObMemstoreFreezer;

// Storage's implementation of the operations demanded by Logservice.
// Observer owns this adapter and binds the two module lifetimes explicitly.
class ObLogStorageAdapter final : public logservice::ObILogStorage
{
public:
  ObLogStorageAdapter();
  ~ObLogStorageAdapter() override;

  int init(ObLSService *ls_service, ObMemstoreFreezer *memstore_freezer);
  void destroy();

  int replay(logservice::ObLogReplayTask *replay_task) override;
  int wait_append_sync() override;
  bool is_replay_pending_log_too_large(int64_t pending_size) override;
  int get_log_handler(logservice::ObLogHandler *&log_handler) override;
  int get_unrecyclable_log_disk_size(
      int64_t &unrecyclable_log_disk_size) override;

private:
  static constexpr int64_t MAX_SINGLE_REPLAY_WARNING_TIME_THRESHOLD =
      100 * 1000; // 100 ms
  static constexpr int64_t MAX_SINGLE_REPLAY_ERROR_TIME_THRESHOLD =
      2 * 1000 * 1000; // 2 s
  static constexpr int64_t MAX_SINGLE_RETRY_WARNING_TIME_THRESHOLD =
      5 * 1000 * 1000; // 5 s

  bool is_inited_;
  ObLSService *ls_service_;
  ObMemstoreFreezer *memstore_freezer_;

  DISALLOW_COPY_AND_ASSIGN(ObLogStorageAdapter);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_TX_STORAGE_OB_LOG_STORAGE_ADAPTER_H_
