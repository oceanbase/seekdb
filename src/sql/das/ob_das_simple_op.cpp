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

#define USING_LOG_PREFIX SQL_DAS
#include "sql/das/ob_das_simple_op.h"
#include "data_plane/ob_i_range_service.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/px/ob_px_sqc_handler.h"

namespace oceanbase
{
namespace sql
{

ObDASSimpleOp::ObDASSimpleOp(ObIAllocator &op_alloc)
  : ObIDASTaskOp(op_alloc) {}

int ObDASSimpleOp::release_op()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObDASSimpleOp::init_task_info(uint32_t row_extend_size)
{
  int ret = OB_SUCCESS;
  UNUSED(row_extend_size);
  return ret;
}

OB_SERIALIZE_MEMBER((ObDASSimpleOp, ObIDASTaskOp));

OB_SERIALIZE_MEMBER(ObDASEmptyCtDef);
OB_SERIALIZE_MEMBER(ObDASEmptyRtDef);

ObDASSplitRangesOp::ObDASSplitRangesOp(ObIAllocator &op_alloc)
  : ObDASSimpleOp(op_alloc), expected_task_count_(0), timeout_us_(0) {}

int ObDASSplitRangesOp::open_op()
{
  int ret = OB_SUCCESS;
  data_plane::ObIRangeService *range_service = ::oceanbase::share::server_service<::oceanbase::data_plane::ObIRangeService>();
  if (OB_FAIL(range_service->split_multi_ranges(tablet_id_,
                                                 timeout_us_,
                                                 ranges_,
                                                 expected_task_count_,
                                                 op_alloc_,
                                                 multi_range_split_array_))) {
    LOG_WARN("failed to split multi ranges", K(ret), K_(tablet_id));
  }
  return ret;
}

int ObDASSplitRangesOp::init(const common::ObIArray<ObStoreRange> &ranges, int64_t expected_task_count, const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  expected_task_count_ = expected_task_count;
  timeout_us_ = timeout_us;
  if (OB_FAIL(ranges_.assign(ranges))) {
    LOG_WARN("failed to assign ranges array", K(ret));
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObDASSplitRangesOp, ObIDASTaskOp),
                     ranges_,
                     expected_task_count_,
                     timeout_us_);

ObDASRangesCostOp::ObDASRangesCostOp(common::ObIAllocator &op_alloc)
  : ObDASSimpleOp(op_alloc), total_size_(0), timeout_us_(0) {}

int ObDASRangesCostOp::open_op()
{
  int ret = OB_SUCCESS;
  data_plane::ObIRangeService *range_service = ::oceanbase::share::server_service<::oceanbase::data_plane::ObIRangeService>();
  if (OB_FAIL(range_service->get_multi_ranges_cost(tablet_id_,
                                                    timeout_us_,
                                                    ranges_,
                                                    total_size_))) {
    LOG_WARN("failed to get multi ranges cost", K(ret), K_(tablet_id));
  }
  return ret;
}

int ObDASRangesCostOp::init(const common::ObIArray<ObStoreRange> &ranges, const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ranges_.assign(ranges))) {
    LOG_WARN("failed to assign ranges array", K(ret));
  }
  timeout_us_ = timeout_us;
  return ret;
}

OB_SERIALIZE_MEMBER((ObDASRangesCostOp, ObIDASTaskOp),
                     ranges_,
                     total_size_,
                     timeout_us_);

int ObDASSimpleUtils::split_multi_ranges(ObExecContext &exec_ctx,
                                         ObDASTabletLoc *tablet_loc,
                                         const common::ObIArray<ObStoreRange> &ranges,
                                         const int64_t expected_task_count,
                                         ObArrayArray<ObStoreRange> &multi_range_split_array)
{
  int ret = OB_SUCCESS;
  ObIDASTaskOp *task_op = nullptr;
  ObDASSplitRangesOp *split_ranges_op = nullptr;
  ObEvalCtx eval_ctx(exec_ctx);
  ObDASRef das_ref(eval_ctx, exec_ctx);
  das_ref.set_mem_attr(ObMemAttr("DASSplitRanges"));
  if (OB_FAIL(das_ref.create_das_task(tablet_loc, DAS_OP_SPLIT_MULTI_RANGES, task_op))) {
    LOG_WARN("prepare das split_multi_ranges task failed", K(ret));
  } else {
    split_ranges_op = static_cast<ObDASSplitRangesOp*>(task_op);
    split_ranges_op->set_can_part_retry(true);
    ObPhysicalPlanCtx *plan_ctx = nullptr;
    if (OB_ISNULL(plan_ctx = exec_ctx.get_physical_plan_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr", K(ret));
    } else if (OB_FAIL(split_ranges_op->init(ranges,
                                             expected_task_count,
                                             plan_ctx->get_timeout_timestamp() - ObTimeUtility::current_time()))) {
      LOG_WARN("failed to init das split ranges op", K(ret));
    } else if (OB_FAIL(das_ref.execute_all_task())) {
      LOG_WARN("execute das split_multi_ranges task failed", K(ret));
    } else if (OB_FAIL(multi_range_split_array.assign(split_ranges_op->get_split_array()))) {
      LOG_WARN("assgin split multi ranges array failed", K(ret));
    } else {
      int64_t count = multi_range_split_array.count();
      // scan range is finally shared by all px workers, use thread safe allocator to avoid data race
      common::ObIAllocator &alloc = *exec_ctx.get_sqc_handler()->get_des_allocator();
      for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
        for (int64_t j = 0; OB_SUCC(ret) && j < multi_range_split_array.count(i); j++) {
          ObStoreRange &store_range = multi_range_split_array.at(i, j);

          // deep copy ObRowKey of store_range
          const ObStoreRowkey &start_key = store_range.get_start_key();
          const ObStoreRowkey &end_key = store_range.get_end_key();
          ObStoreRowkey dst_start_key;
          ObStoreRowkey dst_end_key;
          if (OB_FAIL(start_key.deep_copy(dst_start_key, alloc))) {
            LOG_WARN("failed to deep copy start key", K(start_key), K(ret));
          } else if (OB_FAIL(end_key.deep_copy(dst_end_key, alloc))) {
            LOG_WARN("failed to deep copy end key", K(start_key), K(ret));
          } else {
            store_range.set_start_key(dst_start_key);
            store_range.set_end_key(dst_end_key);
          }
        }
      }
    }
  }
  return ret;
}

int ObDASSimpleUtils::get_multi_ranges_cost(ObExecContext &exec_ctx,
                                            ObDASTabletLoc *tablet_loc,
                                            const common::ObIArray<common::ObStoreRange> &ranges,
                                            int64_t &total_size)
{
  int ret = OB_SUCCESS;
  ObIDASTaskOp *task_op = nullptr;
  ObDASRangesCostOp *ranges_cost_op = nullptr;
  ObEvalCtx eval_ctx(exec_ctx);
  ObDASRef das_ref(eval_ctx, exec_ctx);
  das_ref.set_mem_attr(ObMemAttr("DASGetRangeCost"));
  if (OB_FAIL(das_ref.create_das_task(tablet_loc, DAS_OP_GET_RANGES_COST, task_op))) {
    LOG_WARN("prepare das get_multi_ranges_cost task failed", K(ret));
  } else {
    ranges_cost_op = static_cast<ObDASRangesCostOp*>(task_op);
    ranges_cost_op->set_can_part_retry(true);
    ObPhysicalPlanCtx *plan_ctx = nullptr;
    if (OB_ISNULL(plan_ctx = exec_ctx.get_physical_plan_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr", K(ret));
    } else if (OB_FAIL(ranges_cost_op->init(ranges, plan_ctx->get_timeout_timestamp() - ObTimeUtility::current_time()))) {
      LOG_WARN("failed to init das ranges cost op", K(ret));
    } else if (OB_FAIL(das_ref.execute_all_task())) {
      LOG_WARN("execute das get_multi_ranges_cost task failed", K(ret));
    } else {
      total_size = ranges_cost_op->get_total_size();
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
