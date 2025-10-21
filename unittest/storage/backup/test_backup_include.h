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

#define USING_LOG_PREFIX STORAGE
#include "gtest/gtest.h"
#include "storage/backup/ob_backup_index_merger.h"
#include "storage/blocksstable/ob_logic_macro_id.h"
#define private public
#define protected public

namespace oceanbase
{
namespace backup
{

int64_t max_tablet_id = 0;

/* ObFakeBackupMacroIndexMerger */

class ObFakeBackupMacroIndexMerger : public ObBackupMacroBlockIndexMerger {
public:
  ObFakeBackupMacroIndexMerger();
  virtual ~ObFakeBackupMacroIndexMerger();
  void set_count(const int64_t file_count, const int64_t per_file_item_count)
  {
    file_count_ = file_count;
    per_file_item_count_ = per_file_item_count;
  }

private:
  virtual int get_all_retries_(const int64_t task_id, const uint64_t tenant_id,
      const share::ObBackupDataType &backup_data_type, const share::ObLSID &ls_id, common::ObISQLClient &sql_proxy,
      common::ObIArray<ObBackupRetryDesc> &retry_list) override;
  virtual int alloc_merge_iter_(const bool tenant_level, const ObBackupIndexMergeParam &merge_param,
      const ObBackupRetryDesc &retry_desc, ObIMacroBlockIndexIterator *&iter) override;

private:
  int64_t file_count_;
  int64_t per_file_item_count_;
  DISALLOW_COPY_AND_ASSIGN(ObFakeBackupMacroIndexMerger);
};

}
}
