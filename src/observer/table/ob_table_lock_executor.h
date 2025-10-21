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

#ifndef OCEANBASE_OBSERVER_OB_TABLE_LOCK_EXECUTOR_H
#define OCEANBASE_OBSERVER_OB_TABLE_LOCK_EXECUTOR_H
#include "ob_table_modify_executor.h"
#include "ob_table_scan_executor.h"
#include "ob_table_context.h"
namespace oceanbase
{
namespace table
{

class ObTableApiLockSpec : public ObTableApiModifySpec
{
public:
  typedef common::ObArrayWrap<ObTableLockCtDef*> ObTableLockCtDefArray;
  ObTableApiLockSpec(common::ObIAllocator &alloc, const ObTableExecutorType type)
      : ObTableApiModifySpec(alloc, type),
        lock_ctdefs_()
  {
  }
  int init_ctdefs_array(int64_t size);
  virtual ~ObTableApiLockSpec();
public:
  OB_INLINE const ObTableLockCtDefArray& get_ctdefs() const { return lock_ctdefs_; }
  OB_INLINE ObTableLockCtDefArray& get_ctdefs() { return lock_ctdefs_; }
private:
  ObTableLockCtDefArray lock_ctdefs_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTableApiLockSpec);
};

class ObTableApiLockExecutor : public ObTableApiModifyExecutor
{
public:
  typedef common::ObArrayWrap<ObTableLockRtDef> ObTableLockRtDefArray;
  ObTableApiLockExecutor(ObTableCtx &ctx, const ObTableApiLockSpec &lock_spec)
      : ObTableApiModifyExecutor(ctx),
        lock_spec_(lock_spec),
        lock_rtdefs_(),
        cur_idx_(0)
  {
  }
  ~ObTableApiLockExecutor()
  {
    if (OB_NOT_NULL(child_)) {
      ObTableApiScanExecutor *scan_executor = static_cast<ObTableApiScanExecutor *>(child_);
      scan_executor->~ObTableApiScanExecutor();
    }
  }
public:
  virtual int open();
  virtual int get_next_row();
  virtual int close();
private:
  int generate_lock_rtdef(const ObTableLockCtDef &lock_ctdef, ObTableLockRtDef &lock_rtdef);
  int inner_open_with_das();
  int get_next_row_from_child();
  int lock_row_to_das();
  int lock_rows_post_proc();
private:
  const ObTableApiLockSpec &lock_spec_;
  ObTableLockRtDefArray lock_rtdefs_;
  int64_t cur_idx_;
};

} // end namespace table
} // end namespace oceanbase

#endif /* OCEANBASE_OBSERVER_OB_TABLE_LOCK_EXECUTOR_H */
