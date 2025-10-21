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

#ifndef SRC_STORAGE_OB_SSTABLE_MERGE_INFO_MGR_H_
#define SRC_STORAGE_OB_SSTABLE_MERGE_INFO_MGR_H_

#include "common/ob_simple_iterator.h"
#include "lib/lock/ob_spin_rwlock.h"
#include "lib/container/ob_array.h"
#include "storage/compaction/ob_compaction_suggestion.h"
#include "storage/compaction/ob_sstable_merge_history.h"
#include "share/rc/ob_tenant_base.h"
#include "observer/omt/ob_multi_tenant.h"

namespace oceanbase
{
namespace storage
{

class ObTenantSSTableMergeInfoMgr
{
public:
  static int mtl_init(ObTenantSSTableMergeInfoMgr *&sstable_merge_info);
  static int64_t cal_max();
  static int get_next_info(compaction::ObIDiagnoseInfoMgr::Iterator &major_iter, 
      compaction::ObIDiagnoseInfoMgr::Iterator &minor_iter, 
      compaction::ObSSTableMergeHistory &merge_history, char *buf, const int64_t buf_len);
  ObTenantSSTableMergeInfoMgr();
  virtual ~ObTenantSSTableMergeInfoMgr();
  int init(const int64_t page_size=compaction::ObIDiagnoseInfoMgr::INFO_PAGE_SIZE);
  int add_sstable_merge_info(compaction::ObSSTableMergeHistory &merge_history);
  void reset();
  void destroy();
  int open_iter(compaction::ObIDiagnoseInfoMgr::Iterator &major_iter, 
                compaction::ObIDiagnoseInfoMgr::Iterator &minor_iter);

  int set_max(int64_t max_size);
  int gc_info();

  // for unittest
  int size();

public:
  static const int64_t MEMORY_PERCENTAGE = 1;   // max size = tenant memory size * MEMORY_PERCENTAGE / 100
  static const int64_t MINOR_MEMORY_PERCENTAGE = 75;
  static const int64_t POOL_MAX_SIZE = 32LL * 1024LL * 1024LL; // 32MB

private:
  bool is_inited_;
  compaction::ObIDiagnoseInfoMgr major_info_pool_;
  compaction::ObIDiagnoseInfoMgr minor_info_pool_;
  DISALLOW_COPY_AND_ASSIGN(ObTenantSSTableMergeInfoMgr);
};

}//storage
}//oceanbase

#endif /* SRC_STORAGE_OB_SSTABLE_MERGE_INFO_MGR_H_ */
