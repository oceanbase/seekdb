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

#ifndef OCEANBASE_STORAGE_OB_MDS_TABLE_MERGE_DAG
#define OCEANBASE_STORAGE_OB_MDS_TABLE_MERGE_DAG

#include <stdint.h>
#include "share/scn.h"
#include "storage/compaction/ob_tablet_merge_task.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{
class ObMdsTableMergeDag : public compaction::ObTabletMergeDag
{
public:
  ObMdsTableMergeDag();
  virtual ~ObMdsTableMergeDag() = default;
  ObMdsTableMergeDag(const ObMdsTableMergeDag&) = delete;
  ObMdsTableMergeDag &operator=(const ObMdsTableMergeDag&) = delete;
public:
  virtual int init_by_param(const share::ObIDagInitParam *param) override;
  virtual int create_first_task() override;
  virtual int fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const override;
  virtual int fill_dag_key(char *buf, const int64_t buf_len) const override;

  share::SCN get_flush_scn() const { return flush_scn_; }
  int64_t get_mds_construct_sequence() const { return mds_construct_sequence_; }

  INHERIT_TO_STRING_KV("ObTabletMergeDag", ObTabletMergeDag,
                       K_(is_inited),
                       K_(flush_scn),
                       KTIME_(generate_ts),
                       K_(mds_construct_sequence));
private:
  int fill_compat_mode_();
private:
  bool is_inited_;
  share::SCN flush_scn_;
  int64_t generate_ts_;
  int64_t mds_construct_sequence_;
};
} // namespace mds
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_MDS_TABLE_MERGE_DAG
