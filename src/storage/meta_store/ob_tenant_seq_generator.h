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
#ifndef OCEANBASE_STORAGE_META_STORE_OB_TENANT_SEQ_GENERATOR_H_
#define OCEANBASE_STORAGE_META_STORE_OB_TENANT_SEQ_GENERATOR_H_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/task/ob_timer.h"

namespace oceanbase
{
namespace storage
{
class ObTenantStorageMetaPersister;
struct ObTenantMonotonicIncSeqs
{
public:
  ObTenantMonotonicIncSeqs() : object_seq_(0), tmp_file_seq_(0), write_seq_(0) {}

  TO_STRING_KV(K_(object_seq), K_(tmp_file_seq), K_(write_seq));
  void reset()
  {
    object_seq_ = 0;
    tmp_file_seq_ = 0;
    write_seq_ = 0;
  }
  void set(const uint64_t object_seq, const uint64_t tmp_file_seq, const uint64_t write_seq)
  {
    object_seq_ = object_seq;
    tmp_file_seq_ = tmp_file_seq;
    write_seq_ = write_seq;
  }

  OB_UNIS_VERSION(1);

public:
  uint64_t object_seq_; // meta obj seq
  uint64_t tmp_file_seq_; // temp file seq
  uint64_t write_seq_; // rename_obj in ss for atomic over-write
};


class ObTenantSeqGenerator : public common::ObTimerTask
{
public:
  static const uint64_t BATCH_PREALLOCATE_NUM = 60000;

  ObTenantSeqGenerator()
    : is_inited_(false),
      is_shared_storage_(false),
      tg_id_(-1),
      persister_(nullptr),
      curr_seqs_(),
      preallocated_seqs_() {}

  int init(const bool is_shared_storage, ObTenantStorageMetaPersister &persister);
  int start();
  void stop();
  void destroy();
  int get_private_object_seq(uint64_t &seq);
  int get_tmp_file_seq(uint64_t &seq);
  int get_write_seq(uint64_t &seq);
  virtual void runTimerTask() override;

private:
  int try_preallocate_();


private:

  static const uint64_t PREALLOCATION_TRIGGER_MARGIN = 30000;
  static const uint64_t TRY_PREALLOACTE_INTERVAL = 500000; // 500ms
  static const int64_t MAX_RETRY_TIME = 30;


  bool is_inited_;
  bool is_shared_storage_;
  int tg_id_;
  ObTenantStorageMetaPersister *persister_;
  ObTenantMonotonicIncSeqs curr_seqs_;
  ObTenantMonotonicIncSeqs preallocated_seqs_;
};

} // namespace storage
} // namespace oceanbase
#endif // OCEANBASE_STORAGE_META_STORE_OB_TENANT_SEQ_GENERATOR_H_
