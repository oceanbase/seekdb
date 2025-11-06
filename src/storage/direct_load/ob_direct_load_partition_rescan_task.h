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

#include "storage/direct_load/ob_direct_load_i_merge_task.h"
#include "storage/direct_load/ob_direct_load_merge_ctx.h"

namespace oceanbase
{
namespace storage
{

class ObDirectLoadPartitionRescanTask : public ObDirectLoadIMergeTask
{
public:
  ObDirectLoadPartitionRescanTask();
  virtual ~ObDirectLoadPartitionRescanTask() = default;
  int init(ObDirectLoadTabletMergeCtx *merge_ctx, int64_t thread_cnt, int64_t thread_idx);
  int process() override;
  void stop() override;
  int init_iterator(ObITabletSliceRowIterator *&row_iterator) override { return OB_NOT_IMPLEMENT;}
  ObDirectLoadTabletMergeCtx *get_merge_ctx() override { return merge_ctx_; }
  TO_STRING_KV(KP_(merge_ctx), K_(thread_cnt), K_(thread_idx));

private:
  ObDirectLoadTabletMergeCtx *merge_ctx_;
  int64_t thread_cnt_;
  int64_t thread_idx_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
