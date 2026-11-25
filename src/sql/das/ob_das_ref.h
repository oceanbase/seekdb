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

#ifndef OBDEV_SRC_SQL_DAS_OB_DAS_REF_H_
#define OBDEV_SRC_SQL_DAS_OB_DAS_REF_H_
#include "sql/das/ob_das_task.h"
#include "sql/das/ob_das_define.h"
#include "sql/das/ob_das_factory.h"
#include "sql/das/ob_das_def_reg.h"

namespace oceanbase
{
namespace sql
{
class ObDASScanOp;
class ObDASInsertOp;

struct ObDasAggregatedTask
{
 public:
  ObDasAggregatedTask()
    : tasks_(),
      failed_tasks_(),
      success_tasks_()
  {
  }
  ~ObDasAggregatedTask() { reset(); };
  void reset();
  void reuse();
  int push_back_task(ObIDASTaskOp *das_task);
  // not thread safe
  int move_to_success_tasks(ObIDASTaskOp *das_task);
  // not thread safe
  int move_to_failed_tasks(ObIDASTaskOp *das_task);
  /**
   * get aggregated tasks.
   * @param tasks: task array to output aggregated tasks.
  */
  int get_aggregated_tasks(common::ObIArray<ObIDASTaskOp *> &tasks);

  int get_failed_tasks(common::ObSEArray<ObIDASTaskOp *, 2> &tasks);
  bool has_failed_tasks() const { return failed_tasks_.get_size() > 0; }
  bool has_unstart_tasks() const;
  bool has_not_execute_task() const
  {
    return tasks_.get_size() != 0 || failed_tasks_.get_size() != 0;
  }
  TO_STRING_KV(K(tasks_.get_size()),
               K(failed_tasks_.get_size()),
               K(success_tasks_.get_size()));
  DasTaskLinkedList tasks_;
  DasTaskLinkedList failed_tasks_;
  DasTaskLinkedList success_tasks_;
};

struct DasRefKey
{
public:
  DasRefKey()
    : tablet_access_(NULL),
      op_type_(ObDASOpType::DAS_OP_INVALID),
      vec_datum_kind_(DAS_VEC_DATUM_TEXT)
  {}
  DasRefKey(const DASRuntimeTabletAccess *tablet_access, ObDASOpType op_type,
            DasVecDatumKind vec_datum_kind = DAS_VEC_DATUM_TEXT)
    : tablet_access_(tablet_access),
      op_type_(op_type),
      vec_datum_kind_(vec_datum_kind)
  {}
  ~DasRefKey() {}
  bool operator==(const DasRefKey &other) const;
  uint64_t hash() const;
  int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; }
  TO_STRING_KV(KP_(tablet_access), K_(op_type), K_(vec_datum_kind));

public:
  const DASRuntimeTabletAccess *tablet_access_;
  ObDASOpType op_type_;
  DasVecDatumKind vec_datum_kind_;
};

static const int64_t DAS_REF_TASK_LOOKUP_THRESHOLD = 1000;
static const int64_t DAS_REF_TASK_SIZE_THRESHOLD = 1000;
static const int64_t DAS_REF_MAP_BUCKET_SIZE = 5000;
typedef common::hash::ObHashMap<DasRefKey, ObIDASTaskOp *, common::hash::NoPthreadDefendMode> ObDASRefMap;
typedef common::ObObjStore<ObDasAggregatedTask *, common::ObIAllocator&> DasAggregatedTaskList;

class ObDASRef
{
public:
  explicit ObDASRef(ObEvalCtx &eval_ctx, ObExecContext &exec_ctx);
  ~ObDASRef() { reset(); }

  DASOpResultIter begin_result_iter();
  DASTaskIter begin_task_iter() { return batched_tasks_.begin(); }
  ObDASTaskFactory &get_das_factory() { return das_factory_; }
  void set_mem_attr(const common::ObMemAttr &memattr) { das_alloc_.set_attr(memattr); }
  ObExecContext &get_exec_ctx() { return exec_ctx_; }
  template <typename DASOp>
  bool has_das_op(const DASRuntimeTabletAccess *tablet_access, DASOp *&das_op,
                  DasVecDatumKind vec_datum_kind = DAS_VEC_DATUM_TEXT);
  ObIDASTaskOp* find_das_task(const DASRuntimeTabletAccess *tablet_access, ObDASOpType op_type,
                             DasVecDatumKind vec_datum_kind = DAS_VEC_DATUM_TEXT);
  int add_aggregated_task(ObIDASTaskOp *das_task, ObDASOpType op_type);
  int create_agg_task(ObDASOpType op_type, ObDasAggregatedTask *&agg_task);
  int find_agg_task(ObDASOpType op_type, ObDasAggregatedTask *&agg_task);
  int add_batched_task(ObIDASTaskOp *das_task);
  // Create a DAS Task, and hold it by das_ref
  template <typename DASOp>
  int prepare_das_task(const DASRuntimeTabletAccess *tablet_access, DASOp *&task_op,
                       DasVecDatumKind vec_datum_kind = DAS_VEC_DATUM_TEXT);
  int create_das_task(const DASRuntimeTabletAccess *tablet_access,
                      ObDASOpType op_type,
                     ObIDASTaskOp *&task_op,
                     DasVecDatumKind vec_datum_kind = DAS_VEC_DATUM_TEXT);
  bool has_task() const { return !batched_tasks_.empty(); }
  int32_t get_das_task_cnt() const { return batched_tasks_.get_size(); }
  int execute_all_task();
  int execute_all_task(DasAggregatedTaskList &agg_task_list);
  int retry_all_fail_tasks(common::ObIArray<ObIDASTaskOp *> &failed_tasks);
  int close_all_task();
  bool is_snapshot_opt_enabled() const { return use_snapshot_opt_; }
  void set_use_snapshot_opt(bool v) { use_snapshot_opt_ = v; }
  common::ObIAllocator &get_das_alloc() { return das_alloc_; }
  int64_t get_das_mem_used() const { return das_alloc_.used() - init_mem_used_; }

  int pick_del_task_to_first();
  void print_all_das_task();
  const ObExprFrameInfo *get_expr_frame_info() const { return expr_frame_info_; }
  void set_expr_frame_info(const ObExprFrameInfo *info) { expr_frame_info_ = info; }
  ObEvalCtx &get_eval_ctx() { return eval_ctx_; };
  void reset();
  void reuse();
private:
  DISABLE_COPY_ASSIGN(ObDASRef);
  int create_task_map();
  bool check_rcode_can_retry(int ret);
private:
  typedef common::ObObjNode<ObIDASTaskOp*> DasOpNode;
  //declare das allocator
  common::ObWrapperAllocatorWithAttr das_alloc_;
  common::ObArenaAllocator *reuse_alloc_;
  union {
    common::ObArenaAllocator reuse_alloc_buf_;
  };
  /////
  ObDASTaskFactory das_factory_;
  // A SQL Operator can produce multiple das tasks and be executed in batch by DAS
  DasTaskList batched_tasks_;
  ObExecContext &exec_ctx_;
  ObEvalCtx &eval_ctx_;
  const ObExprFrameInfo *expr_frame_info_;
  DasAggregatedTaskList del_aggregated_tasks_; // we must execute all delete tasks firstly

  DasAggregatedTaskList aggregated_tasks_;
  int64_t lookup_cnt_;
  int64_t task_cnt_;
  int64_t init_mem_used_;
  ObDASRefMap task_map_;
public:
  //all flags
  union {
    uint64_t flags_;
    struct { // FARM COMPAT WHITELIST
      uint64_t use_snapshot_opt_                : 1;
      uint64_t reserved_                        : 63;
    };
  };
};

template <typename DASOp>
OB_INLINE bool ObDASRef::has_das_op(const DASRuntimeTabletAccess *tablet_access, DASOp *&das_op,
                                   DasVecDatumKind vec_datum_kind)
{
  ObDASOpType type = static_cast<ObDASOpType>(das_reg::ObDASOpTraits<DASOp>::type_);
  bool bret = false;
  ObIDASTaskOp *das_task = find_das_task(tablet_access, type, vec_datum_kind);
  if (das_task != nullptr) {
    bret = true;
    OB_ASSERT(typeid(*das_task) == typeid(DASOp));
    das_op = static_cast<DASOp*>(das_task);
  }
  return bret;
}

template <typename DASOp>
OB_INLINE int ObDASRef::prepare_das_task(const DASRuntimeTabletAccess *tablet_access, DASOp *&task_op,
                                        DasVecDatumKind vec_datum_kind)
{
  ObDASOpType type = static_cast<ObDASOpType>(das_reg::ObDASOpTraits<DASOp>::type_);
  ObIDASTaskOp *tmp_op = nullptr;
  int ret = create_das_task(tablet_access, type, tmp_op, vec_datum_kind);
  if (OB_SUCC(ret)) {
    task_op = static_cast<DASOp*>(tmp_op);
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
#endif /* OBDEV_SRC_SQL_DAS_OB_DAS_REF_H_ */
