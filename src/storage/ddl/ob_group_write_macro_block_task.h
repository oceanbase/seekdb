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

#ifndef _OCEANBASE_STORAGE_DDL_OB_GROUP_WRTIE_MACRO_BLOCK_TASK_H_
#define _OCEANBASE_STORAGE_DDL_OB_GROUP_WRTIE_MACRO_BLOCK_TASK_H_

#include "share/scheduler/ob_tenant_dag_scheduler.h"

namespace oceanbase
{
namespace blocksstable
{
struct ObDatumRow;
struct ObMacroDataSeq;
}

namespace storage
{

class ObDDLIndependentDag;
struct ObDDLTabletContext;
class ObCGBlockFile;

class ObGroupWriteMacroBlockTask: public share::ObITask
{
public:
  ObGroupWriteMacroBlockTask();
  virtual ~ObGroupWriteMacroBlockTask();
  int init(ObDDLIndependentDag *ddl_dag);
  int init(ObDDLIndependentDag *ddl_dag, const ObTabletID &tablet_id);
  int process();
  virtual void task_debug_info_to_string(char *buf, const int64_t buf_len, int64_t &pos) const override;
  int group_write_macro_block(const ObTabletID &tablet_id);
  int schedule_write_task(const ObTabletID &tablet_id, const int64_t slice_idx, const int64_t cg_idx, ObIArray<ObCGBlockFile *> &group_files);
private:
  ObDDLIndependentDag *ddl_dag_;
  ObTabletID tablet_id_;
  ObArray<ObITask *> group_write_tasks_;
};

class ObGroupCGBlockFileWriteTask : public share::ObITask
{
public:
  ObGroupCGBlockFileWriteTask();
  virtual ~ObGroupCGBlockFileWriteTask();
  int init(ObDDLIndependentDag *ddl_dag, const ObTabletID &tablet_id, const int64_t slice_idx, const int64_t cg_idx, ObIArray<ObCGBlockFile *> &group_files);
  int process();
  virtual void task_debug_info_to_string(char *buf, const int64_t buf_len, int64_t &pos) const override;
  void reset();

private:
  bool is_inited_;
  ObDDLIndependentDag *ddl_dag_;
  ObTabletID tablet_id_;
  int64_t slice_idx_;
  int64_t cg_idx_;
  ObArray<ObCGBlockFile *> block_files_;

};

} // end namespace storage
} // end namespace oceanbase

#endif//_OCEANBASE_STORAGE_DDL_OB_GROUP_WRTIE_MACRO_BLOCK_TASK_H_
