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

#ifndef OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_ELIMINATION_MANAGER_H_
#define OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_ELIMINATION_MANAGER_H_

#include "storage/blocksstable/ob_macro_block_id.h"
#include "storage/tmp_file/ob_shared_nothing_tmp_file.h"
#include "lib/allocator/ob_fifo_allocator.h"
#include "lib/list/ob_dlist.h"
#include "lib/lock/ob_spin_lock.h"

namespace oceanbase
{
namespace tmp_file
{
class ObTmpFileEvictionManager
{
public:
  typedef ObSharedNothingTmpFile::ObTmpFileNode TmpFileNode;
  typedef common::ObDList<TmpFileNode> TmpFileEvictionList;

public:
  ObTmpFileEvictionManager() : data_list_lock_(), file_data_eviction_list_(),
                               meta_list_lock_(), file_meta_eviction_list_() {}
  ~ObTmpFileEvictionManager() { destroy(); }
  void destroy();
  int64_t get_file_size();
  int add_file(const bool is_meta, ObSharedNothingTmpFile &file);
  int remove_file(ObSharedNothingTmpFile &file);
  int remove_file(const bool is_meta, ObSharedNothingTmpFile &file);
  int evict(const int64_t expected_evict_page_num, int64_t &actual_evict_page_num);

private:
  int evict_file_from_list_(const bool &is_meta, const int64_t expected_evict_page_num, int64_t &actual_evict_page_num);
  int pop_file_from_list_(const bool &is_meta, ObSNTmpFileHandle &file_handle);
private:
  ObSpinLock data_list_lock_;
  TmpFileEvictionList file_data_eviction_list_;
  ObSpinLock meta_list_lock_;
  TmpFileEvictionList file_meta_eviction_list_;
};

}  // end namespace tmp_file
}  // end namespace oceanbase

#endif // OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_ELIMINATION_MANAGER_H_
