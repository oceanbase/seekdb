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

#ifndef OCEANBASE_STORAGE_OB_TABLET_MDS_MERGE_CTX
#define OCEANBASE_STORAGE_OB_TABLET_MDS_MERGE_CTX

#include "storage/compaction/ob_tablet_merge_ctx.h"

namespace oceanbase
{
namespace common
{
class ObArenaAllocator;
}

namespace compaction
{
struct ObTabletMergeDagParam;
}

namespace storage
{
class ObTabletMdsMinorMergeCtx : public compaction::ObTabletExeMergeCtx
{
public:
  ObTabletMdsMinorMergeCtx(compaction::ObTabletMergeDagParam &param, common::ObArenaAllocator &allocator);
  virtual ~ObTabletMdsMinorMergeCtx() { free_schema(); }
protected:
  virtual int prepare_schema() override;
  virtual int prepare_index_tree() override;
  virtual void free_schema() override;
  virtual int get_merge_tables(ObGetMergeTablesResult &get_merge_table_result) override;
  virtual int update_tablet(ObTabletHandle &new_tablet_handle) override;
};

class ObTabletCrossLSMdsMinorMergeCtx : public compaction::ObTabletMergeCtx
{
public:
  ObTabletCrossLSMdsMinorMergeCtx(compaction::ObTabletMergeDagParam &param, common::ObArenaAllocator &allocator);
  virtual ~ObTabletCrossLSMdsMinorMergeCtx() { free_schema(); }
public:
  virtual int prepare_schema() override;
  virtual int prepare_index_tree() override;
  virtual void free_schema() override;
  virtual int get_merge_tables(ObGetMergeTablesResult &get_merge_table_result) override;
  virtual int update_tablet(ObTabletHandle &new_tablet_handle) override;

private:
  int prepare_compaction_filter();

};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_MDS_MERGE_CTX
