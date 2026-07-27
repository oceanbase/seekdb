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

#ifndef OBDEV_SRC_SQL_DAS_OB_DAS_TASK_H_
#define OBDEV_SRC_SQL_DAS_OB_DAS_TASK_H_
#include "share/ob_define.h"
#include "storage/tx/ob_trans_define.h"
#include "sql/das/ob_das_define.h"
#include "storage/access/ob_dml_param.h"
#include "lib/list/ob_obj_store.h"
namespace oceanbase
{
namespace common
{
class ObNewRowIterator;
}  // namespace common
namespace sql
{
class ObDASScanOp;
class ObDASTaskFactory;
class ObDasAggregatedTask;

typedef ObDLinkNode<ObIDASTaskOp*> DasTaskNode;
typedef ObDList<DasTaskNode> DasTaskLinkedList;

struct ObDASSnapshotOptInfo
{
  OB_UNIS_VERSION(1);
public:
  ObDASSnapshotOptInfo(common::ObIAllocator &alloc)
    : alloc_(alloc),
      use_specify_snapshot_(false),
      isolation_level_(),
      specify_snapshot_(nullptr),
      response_snapshot_(nullptr)
  {
  }

  ~ObDASSnapshotOptInfo()
  {
    if (specify_snapshot_ != nullptr) {
      specify_snapshot_->~ObTxReadSnapshot();
    }
    if (response_snapshot_ != nullptr) {
      response_snapshot_->~ObTxReadSnapshot();
    }
  }

  int init(transaction::ObTxIsolationLevel isolation_level);
  void set_use_specify_snapshot(bool v)
  {
    use_specify_snapshot_ = v;
  }
  bool get_use_specify_snapshot() { return use_specify_snapshot_; }
  transaction::ObTxReadSnapshot *get_specify_snapshot() { return specify_snapshot_; }
  transaction::ObTxReadSnapshot *get_response_snapshot() { return response_snapshot_; }

  TO_STRING_KV(K_(use_specify_snapshot),
               K_(isolation_level),
               KPC_(specify_snapshot),
               KPC_(response_snapshot));
  common::ObIAllocator &alloc_; // inited by op_alloc_ in das_op
  bool use_specify_snapshot_;
  transaction::ObTxIsolationLevel isolation_level_;
  transaction::ObTxReadSnapshot *specify_snapshot_; // specify snapshot_version for task
  transaction::ObTxReadSnapshot *response_snapshot_;
};

struct ObDASCopyContext
{
public:
  ObDASCopyContext() : ctdefs_(), rtdefs_() {}
  OB_INLINE static ObDASCopyContext *&get_copy_context()
  {
    RLOCAL_INLINE(ObDASCopyContext*, g_copy_context);
    return g_copy_context;
  }
  common::ObSEArray<const ObDASBaseCtDef*, 2> ctdefs_;
  common::ObSEArray<ObDASBaseRtDef*, 2> rtdefs_;
};

class ObIDASTaskOp
{
  friend class ObDataAccessService;
  friend class ObDASRef;
  friend class ObDASParallelHandler;
  OB_UNIS_VERSION_V(1);
public:
  ObIDASTaskOp(common::ObIAllocator &op_alloc)
    : errcode_(OB_SUCCESS),
      trans_desc_(nullptr),
      snapshot_(nullptr),
      task_id_(common::OB_INVALID_ID),
      op_type_(DAS_OP_INVALID),
      task_flag_(0),
      write_branch_id_(0),
      tablet_loc_(nullptr),
      op_alloc_(op_alloc),
      related_ctdefs_(op_alloc),
      related_rtdefs_(op_alloc),
      related_tablet_ids_(op_alloc),
      task_status_(ObDasTaskStatus::UNSTART),
      das_task_node_(),
      agg_task_(nullptr),
      cur_agg_list_(nullptr),
      attach_ctdef_(nullptr),
      attach_rtdef_(nullptr),
      das_snapshot_opt_info_(op_alloc),
      plan_line_id_(0),
      das_task_start_timestamp_(0)
  {
    das_task_node_.get_data() = this;
  }
  virtual ~ObIDASTaskOp() { }

  virtual int open_op() = 0; // Execute specific DAS Task Op logic, customized by the instantiated TaskOp
  virtual int release_op() = 0; //close DAS Task Op, release the corresponding resources
  virtual int record_task_result_to_rtdef() = 0;
  virtual int assign_task_result(ObIDASTaskOp *other) = 0;
  void set_tablet_id(const common::ObTabletID &tablet_id) { tablet_id_ = tablet_id; }
  const common::ObTabletID &get_tablet_id() const { return tablet_id_; }
  void set_task_id(const int64_t task_id) { task_id_ = task_id; }
  int64_t get_task_id() const { return task_id_; }
  void set_tablet_loc(const ObDASTabletLoc *tablet_loc) { tablet_loc_ = tablet_loc; }
  // tablet_loc_ will not be serialized, therefore it cannot be accessed during the execution phase
  // of DASTaskOp. It can only be touched through das_ref and data_access_service layer.
  const ObDASTabletLoc *get_tablet_loc() const { return tablet_loc_; }
  inline int64_t get_ref_table_id() const { return tablet_loc_->loc_meta_->ref_table_id_; }
  virtual int init_task_info(uint32_t row_extend_size) = 0;
  virtual const ObDASBaseCtDef *get_ctdef() const { return nullptr; }
  virtual ObDASBaseRtDef *get_rtdef() { return nullptr; }
  virtual void reset_access_datums_ptr() { }
  DASCtDefFixedArray &get_related_ctdefs() { return related_ctdefs_; }
  DASRtDefFixedArray &get_related_rtdefs() { return related_rtdefs_; }
  ObTabletIDFixedArray &get_related_tablet_ids() { return related_tablet_ids_; }
  virtual int dump_data() const { return common::OB_SUCCESS; }
  const DasTaskNode &get_node() const { return das_task_node_; }
  DasTaskNode &get_node() { return das_task_node_; }
  int get_errcode() const { return errcode_; }
  void set_errcode(int errcode) { errcode_ = errcode; }
  void set_plan_line_id(int64_t plan_line_id) { plan_line_id_ = plan_line_id; }
  int64_t get_plan_line_id() const { return plan_line_id_; }
  void set_attach_ctdef(const ObDASBaseCtDef *attach_ctdef) { attach_ctdef_ = attach_ctdef; }
  void set_attach_rtdef(ObDASBaseRtDef *attach_rtdef) { attach_rtdef_ = attach_rtdef; }
  ObDASBaseRtDef *get_attach_rtdef() { return attach_rtdef_; }
  VIRTUAL_TO_STRING_KV(K_(task_id),
                       K_(op_type),
                       K_(errcode),
                       K_(can_part_retry),
                       K_(task_started),
                       K_(in_part_retry),
                       K_(in_stmt_retry),
                       KPC_(trans_desc),
                       KPC_(snapshot),
                       K_(tablet_id),
                       KPC_(tablet_loc),
                       K_(related_ctdefs),
                       K_(related_rtdefs),
                       K_(task_status),
                       K_(related_tablet_ids),
                       K_(das_task_node),
                       K_(plan_line_id));
public:
  
  
  void set_type(ObDASOpType op_type) { op_type_ = op_type; }
  ObDASOpType get_type() const { return op_type_; }
  void set_trans_desc(transaction::ObTxDesc *trans_desc) { trans_desc_ = trans_desc; }
  transaction::ObTxDesc *get_trans_desc() { return trans_desc_; }
  void set_snapshot(transaction::ObTxReadSnapshot *snapshot) { snapshot_ = snapshot; }
  transaction::ObTxReadSnapshot *get_snapshot() { return snapshot_; }
  int16_t get_write_branch_id() const { return write_branch_id_; }
  void set_write_branch_id(const int16_t branch_id) { write_branch_id_ = branch_id; }
  bool is_local_task() const { return task_started_; }
  void set_can_part_retry(const bool flag) { can_part_retry_ = flag; }
  bool can_part_retry() const { return can_part_retry_; }
  bool is_in_retry() const { return in_part_retry_ || in_stmt_retry_; }
  void set_task_status(ObDasTaskStatus status);
  ObDasTaskStatus get_task_status() const { return task_status_; };
  const ObDasAggregatedTask *get_agg_task() const { return agg_task_; };
  ObDasAggregatedTask *get_agg_task() { return agg_task_; };
  void set_agg_task(ObDasAggregatedTask *agg_task)
  {
    OB_ASSERT(agg_task != nullptr);
    OB_ASSERT(agg_task_ == nullptr);
    agg_task_ = agg_task;
  };
  // Not thread safe. State advances only on the task's scheduling thread.
  int state_advance();
  void set_cur_agg_list(DasTaskLinkedList *list) { cur_agg_list_ = list; };
  DasTaskLinkedList *get_cur_agg_list() { return cur_agg_list_; };

  bool get_inner_rescan()          { return inner_rescan_; }
  void set_inner_rescan(bool flag) { inner_rescan_ = flag; }
  void set_write_buff_full(bool v) { write_buff_full_ = v; }
  bool is_write_buff_full() { return write_buff_full_; }
  ObDASSnapshotOptInfo &get_das_snapshot_opt_info() { return das_snapshot_opt_info_; }
  int init_das_snapshot_opt_info(transaction::ObTxIsolationLevel isolation_level);

protected:
  int start_das_task();
  int end_das_task();

public:
  int errcode_; //don't need serialize it
  transaction::ObTxDesc *trans_desc_; // transaction state is owned by the SQL session
  transaction::ObTxReadSnapshot *snapshot_; // Mvcc snapshot

protected:
  int64_t task_id_;
  ObDASOpType op_type_; // DAS provided operation type
protected:
  // transaction related information
  union
  {
    uint32_t task_flag_;
    struct
    {
      /*the first 16 bits are static flags*/
      uint16_t can_part_retry_   : 1;
      uint16_t flag_reserved_    : 15;
      /*the last 16 bits are status masks*/
      uint16_t task_started_     : 1;
      uint16_t in_part_retry_    : 1;
      uint16_t in_stmt_retry_    : 1;
      uint16_t inner_rescan_ : 1; //disable das retry for inner_rescan
      uint16_t write_buff_full_  : 1;
      uint16_t status_reserved_  : 11;
    };
  };
  int16_t write_branch_id_;  // branch id for parallel write, required for partially rollback
  common::ObTabletID tablet_id_;
  // tablet_loc_ will not be serialized, therefore it cannot be accessed during the execution phase
  // of DASTaskOp. It can only be touched through das_ref and data_access_service layer.
  const ObDASTabletLoc *tablet_loc_;
  common::ObIAllocator &op_alloc_;
  //In DML DAS Task,related_ctdefs_ means related local index ctdefs
  //In Scan DAS Task for normal secondary index, related_ctdefs_ have only one element, means the lookup ctdef
  //In Scan DAS TASK for domain index, related_ctdefs_ means related local index scan ctdefs,
  //For detailed arrangement information, please refer to the description in ObDASScanOp.
  //The related_ctdef is used solely to retain the fundamental computational information executed with the data table and its index table,
  //such as insert_ctdef, scan_ctdef, etc.
  //It does not include other pushed-down operations bound and executed with the task,
  //such as aux lookup ctdef, etc.
  DASCtDefFixedArray related_ctdefs_;
  DASRtDefFixedArray related_rtdefs_;
  //The related_tablet_ids_ usually correspond to the related_ctdefs information.
  ObTabletIDFixedArray related_tablet_ids_;
  ObDasTaskStatus task_status_;  // do not serialize
  DasTaskNode das_task_node_;  // tasks's linked list node, do not serialize
  ObDasAggregatedTask *agg_task_;  //task's agg task, do not serialize
  DasTaskLinkedList *cur_agg_list_;  //task's agg_list, do not serialize
  //The attach_ctdef describes the computations that are pushed down and executed as an attachment to the ObDASTaskOp,
  //such as the back table operation for full-text indexes,
  //rowkey merging for index merge operations, and so on.
  const ObDASBaseCtDef *attach_ctdef_;
  ObDASBaseRtDef *attach_rtdef_;
  ObDASSnapshotOptInfo das_snapshot_opt_info_;
  int64_t plan_line_id_; //plan operator id
public:
  int64_t das_task_start_timestamp_;

};
typedef common::ObObjStore<ObIDASTaskOp*, common::ObIAllocator&> DasTaskList;
typedef DasTaskList::Iterator DASTaskIter;

class DASOpResultIter
{
public:
  struct WildDatumPtrInfo
  {
    WildDatumPtrInfo(ObEvalCtx &eval_ctx)
      : exprs_(nullptr),
        eval_ctx_(eval_ctx),
        max_output_rows_(0),
        lookup_iter_(nullptr)
    { }
    const ObExprPtrIArray *exprs_;
    ObEvalCtx &eval_ctx_;
    int64_t max_output_rows_;
    // A global index scan and its lookup can share expressions. Associate the
    // two iterators so resetting either side also restores the shared datums.
    DASOpResultIter *lookup_iter_;
  };
public:
  DASOpResultIter()
    : task_iter_(),
      wild_datum_info_(nullptr)
  { }
  DASOpResultIter(const DASTaskIter &task_iter,
                  WildDatumPtrInfo &wild_datum_info)
    : task_iter_(task_iter),
      wild_datum_info_(&wild_datum_info)
  {
  }
  int get_next_row();
  int next_result();
  const ObDASTabletLoc *get_tablet_loc() const { return (*task_iter_)->get_tablet_loc(); }
  bool is_end() const { return task_iter_.is_end(); }
private:
  int reset_wild_datums_ptr();
private:
  DASTaskIter task_iter_;
  WildDatumPtrInfo *wild_datum_info_;
};

template <typename T>
struct DASCtRefEncoder
{
  static int encode(char *buf, const int64_t buf_len, int64_t &pos, const T *val)
  {
    int ret = common::OB_SUCCESS;
    int64_t idx = common::OB_INVALID_INDEX;
    const ObDASBaseCtDef *ctdef = val;
    ObDASCopyContext *copy_context = ObDASCopyContext::get_copy_context();
    if (OB_ISNULL(copy_context)) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_DAS_LOG(WARN, "copy context is nullptr", K(ret));
    } else if (OB_ISNULL(val)) {
      idx = common::OB_INVALID_INDEX;
    } else if (!common::has_exist_in_array(copy_context->ctdefs_, ctdef, &idx)) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_DAS_LOG(WARN, "val not found in ctdefs", K(ret), K(val), KPC(val));
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(common::serialization::encode_i32(buf, buf_len, pos, static_cast<int32_t>(idx)))) {
        SQL_DAS_LOG(WARN, "encode idx failed", K(ret), K(idx));
      }
    }
    return ret;
  }

  static int decode(const char *buf, const int64_t data_len, int64_t &pos, const T *&val)
  {
    int ret = common::OB_SUCCESS;
    int32_t idx = common::OB_INVALID_INDEX;
    ObDASCopyContext *copy_context = ObDASCopyContext::get_copy_context();
    if (OB_ISNULL(copy_context)) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_DAS_LOG(WARN, "copy context is nullptr", K(ret));
    } else if (OB_FAIL(common::serialization::decode_i32(buf, data_len, pos, &idx))) {
      SQL_DAS_LOG(WARN, "decode idx failed", K(ret), K(idx));
    } else if (OB_UNLIKELY(common::OB_INVALID_INDEX == idx)) {
      val = nullptr;
    } else if (OB_UNLIKELY(idx < 0) || OB_UNLIKELY(idx >= copy_context->ctdefs_.count())) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_DAS_LOG(WARN, "idx is invalid", K(ret), K(idx), K(copy_context->ctdefs_.count()));
    } else {
      val = static_cast<const T *>(copy_context->ctdefs_.at(idx));
    }
    return ret;
  }

  static int64_t encoded_length(const T *val)
  {
    UNUSED(val);
    int32_t idx = common::OB_INVALID_INDEX;
    return common::serialization::encoded_length_i32(idx);
  }
};

template <typename T>
struct DASRtRefEncoder
{
  static int encode(char *buf, const int64_t buf_len, int64_t &pos, const T *val)
  {
    int ret = common::OB_SUCCESS;
    int64_t idx = common::OB_INVALID_INDEX;
    ObDASBaseRtDef *rtdef = const_cast<T*>(val);
    ObDASCopyContext *copy_context = ObDASCopyContext::get_copy_context();
    if (OB_ISNULL(copy_context)) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_DAS_LOG(WARN, "copy context is nullptr", K(ret), K(val));
    } else if (OB_ISNULL(val)) {
      idx = common::OB_INVALID_INDEX;
    } else if (!common::has_exist_in_array(copy_context->rtdefs_, rtdef, &idx)) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_DAS_LOG(WARN, "val not found in rtdefs", K(ret), K(val), KPC(val));
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(common::serialization::encode_i32(buf, buf_len, pos, static_cast<int32_t>(idx)))) {
        SQL_DAS_LOG(WARN, "encode idx failed", K(ret), K(idx));
      }
    }
    return ret;
  }

  static int decode(const char *buf, const int64_t data_len, int64_t &pos, T *&val)
  {
    int ret = common::OB_SUCCESS;
    int32_t idx = 0;
    ObDASCopyContext *copy_context = ObDASCopyContext::get_copy_context();
    if (OB_ISNULL(copy_context)) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_DAS_LOG(WARN, "copy context is nullptr", K(ret));
    } else if (OB_FAIL(common::serialization::decode_i32(buf, data_len, pos, &idx))) {
      SQL_DAS_LOG(WARN, "decode idx failed", K(ret), K(idx));
    } else if (OB_UNLIKELY(common::OB_INVALID_INDEX == idx)) {
      val = nullptr;
    } else if (OB_UNLIKELY(idx < 0) || OB_UNLIKELY(idx >= copy_context->rtdefs_.count())) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_DAS_LOG(WARN, "idx is invalid", K(ret), K(idx), K(copy_context->rtdefs_.count()));
    } else {
      val = static_cast<T *>(copy_context->rtdefs_.at(idx));
    }
    return ret;
  }

  static int64_t encoded_length(const T *val)
  {
    UNUSED(val);
    int32_t idx = 0;
    return common::serialization::encoded_length_i32(idx);
  }
};

}  // namespace sql
}  // namespace oceanbase
#endif /* OBDEV_SRC_SQL_DAS_OB_DAS_TASK_H_ */
