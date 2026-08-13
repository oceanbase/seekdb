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

#ifndef OB_OCEANBASE_STORAGE_DDL_DDL_PIPELINE_H
#define OB_OCEANBASE_STORAGE_DDL_DDL_PIPELINE_H

#include "storage/ddl/ob_pipeline.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "common/ob_tablet_id.h"

namespace oceanbase
{
namespace storage
{

class ObIDDLPipeline : public ObPipeline
{
public:
  explicit ObIDDLPipeline(const share::ObITask::ObITaskType &task_type)
    : ObPipeline(task_type)
  {}
  virtual ~ObIDDLPipeline() = default;
  int init(const ObTabletID &tablet_id, const int64_t slice_idx);
  virtual int preprocess() { return OB_SUCCESS; }
  virtual void postprocess(int &ret_code) { UNUSED(ret_code); }
  virtual int get_next_chunk(ObChunk *&chunk) = 0;
  virtual int finish_chunk(ObChunk *chunk) { UNUSED(chunk); return OB_SUCCESS; }
  virtual int process() override;
private:
  ObTabletID tablet_id_;
  int64_t slice_idx_;
};

class ObWriteMacroPipeline : public ObIDDLPipeline
{
public:
  explicit ObWriteMacroPipeline(const share::ObITask::ObITaskType &task_type)
    : ObIDDLPipeline(task_type)
  {}
  virtual ~ObWriteMacroPipeline() = default;
protected:
  virtual int fill_writer_param(ObWriteMacroParam &param) = 0;
protected:
  ObWriteMacroParam write_param_;
};

class ObDDLWriteMacroBlockBasePipeline : public ObWriteMacroPipeline
{
public:
  explicit ObDDLWriteMacroBlockBasePipeline(const share::ObITask::ObITaskType &task_type) :
    ObWriteMacroPipeline(task_type), ddl_slice_(nullptr) { }
  virtual ~ObDDLWriteMacroBlockBasePipeline() = default;
  virtual int get_next_chunk(ObChunk *&chunk) override;
  virtual int finish_chunk(ObChunk *chunk) override;
  virtual void postprocess(int &ret_code) override;
  virtual ObITaskPriority get_priority() override;

protected:
  virtual int fill_writer_param(ObWriteMacroParam &param) override;

protected:
  ObDDLSlice *ddl_slice_;
};

} // namespace storage
} // namespace oceanbase

#endif // OB_OCEANBASE_STORAGE_DDL_DDL_PIPELINE_H
