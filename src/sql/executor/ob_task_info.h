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

#ifndef OCEANBASE_SQL_EXECUTOR_TASK_INFO_
#define OCEANBASE_SQL_EXECUTOR_TASK_INFO_

#include "sql/engine/ob_operator.h"
#include "sql/executor/ob_task_location.h"
#include "sql/executor/ob_task_id.h"
#include "sql/executor/ob_slice_id.h"
#include "sql/executor/ob_task_event.h"
#include "common/ob_range.h"

namespace oceanbase
{
namespace sql
{

//class ObTaskResultV2
//{
//public:
//  ObTaskResultV2() {}
//  virtual ~ObTaskResultV2() {}
//  virtual int64_t get_slice_count() = 0;
//  virtual int get_slice_event(int64_t idx, ObSliceEvent &slice_event) = 0;
//public:
//  void set_task_location(const ObTaskLocation &task_location) { task_location_ = task_location; }
//  ObTaskLocation &get_task_location() { return task_location_; }
//protected:
//  ObTaskLocation task_location_;
//};

//class ObSingleSliceResult : public ObTaskResultV2
//{
//  OB_UNIS_VERSION(1);
//public:
//  ObSingleSliceResult() {}
//  virtual ~ObSingleSliceResult() {}
//  virtual int64_t get_slice_count();
//  virtual int get_slice_event(int64_t idx, ObSliceEvent &slice_event);
//public:
//  void set_slice_event(const ObSliceEvent &slice_event) { slice_event_.assign(slice_event); }
//  int assign(const ObSingleSliceResult &other);
//  TO_STRING_KV(K_(task_location),
//               K_(slice_event));
//private:
//  ObSliceEvent slice_event_;
//};

//class ObMultiSliceResult : public ObTaskResultV2
//{
//public:
//  ObMultiSliceResult() {}
//  virtual ~ObMultiSliceResult() {}
//  virtual int64_t get_slice_count();
//  virtual int get_slice_event(int64_t idx, ObSliceEvent &slice_event);
//public:
//  void set_slice_events(const common::ObIArray<ObSliceEvent> &slice_events) { slice_events_ = &slice_events; }
//private:
//  const common::ObIArray<ObSliceEvent> *slice_events_;
//};

enum ObTaskState
{
  OB_TASK_STATE_NOT_INIT,
  OB_TASK_STATE_INITED,
  OB_TASK_STATE_RUNNING,
  OB_TASK_STATE_FINISHED,
  OB_TASK_STATE_SKIPPED,
  OB_TASK_STATE_FAILED,
};

class ObPhyOperator;
class ObDASTabletLoc;

class ObGranuleTaskInfo
{
public:
	ObGranuleTaskInfo()
	  : ranges_(),
      ss_ranges_(),
	    tablet_loc_(nullptr),
	    task_id_(0)
	{ }
	virtual ~ObGranuleTaskInfo() { }
  int assign(const ObGranuleTaskInfo &other);
	TO_STRING_KV(K_(ranges), K_(ss_ranges), K_(task_id), "tablet_id: ",
               OB_ISNULL(tablet_loc_) ? OB_INVALID_ID : tablet_loc_->tablet_id_.id());
public:
  common::ObSEArray<common::ObNewRange, 1> ranges_;
  common::ObSEArray<common::ObNewRange, 1> ss_ranges_;
  ObDASTabletLoc *tablet_loc_;
  //just for print
  int64_t task_id_;
};
// Used for NLJ scenario to perform partition pruning on the right-side partition table scan
class ObGIPruningInfo
{
public:
  ObGIPruningInfo() : part_id_(common::OB_INVALID_ID) {}

  int64_t get_part_id() const
  {
    return part_id_;
  }
  void set_part_id(int64_t part_id)
  {
    part_id_ = part_id;
  }
private:
  // Non-cropped partition id, all other ids are cropped
  // NLJ updates this part_id once for each row read from the left side
  int64_t part_id_;
};

typedef common::hash::ObHashMap<uint64_t, ObGranuleTaskInfo, common::hash::NoPthreadDefendMode> GIPrepareTaskMap;

class ObTaskInfo
{
public:
  class ObPartLoc
  {
  public:
    ObPartLoc()
      : scan_ranges_(common::ObModIds::OB_SQL_EXECUTOR_TASK_INFO, OB_MALLOC_NORMAL_BLOCK_SIZE),
        part_key_ref_id_(common::OB_INVALID_ID),
        value_ref_id_(common::OB_INVALID_ID),
        renew_time_(common::OB_INVALID_TIMESTAMP),
        row_store_(NULL),
        datum_store_(NULL)
    {
    }
    virtual ~ObPartLoc() {}
    inline void reset()
    {
      scan_ranges_.reset();
      renew_time_ = common::OB_INVALID_TIMESTAMP;
      part_key_ref_id_ = common::OB_INVALID_ID;
      value_ref_id_ = common::OB_INVALID_ID;
      row_store_ = NULL;
      datum_store_ = NULL;
    }
    inline bool is_valid() const
    {
      return common::OB_INVALID_TIMESTAMP != renew_time_;
    }
    TO_STRING_KV(K_(scan_ranges),
                 K_(part_key_ref_id),
                 K_(value_ref_id),
                 K_(renew_time),
                 KPC_(row_store),
                 KPC_(datum_store));
    common::ObSEArray<common::ObNewRange, 1> scan_ranges_; // scan query ranges
    uint64_t part_key_ref_id_; // used to identify which physical operator the partition info in range location belongs to in the plan
    uint64_t value_ref_id_;
    int64_t renew_time_;
    ObRowStore *row_store_; // the row cache corresponding to this partition, used for dml statements
    ObChunkDatumStore *datum_store_;
  };
  class ObRangeLocation
  {
  public:
    ObRangeLocation()
      : inner_alloc_("RangeLocation"),
        part_locs_(&inner_alloc_)
    {
    }
    explicit ObRangeLocation(common::ObIAllocator &allocator)
      : part_locs_(allocator),
        server_()
    {
    }
    virtual ~ObRangeLocation() {}
    inline void reset()
    {
      part_locs_.reset();
      server_.reset();
    }
    inline bool is_valid() const
    {
      bool bool_ret = true;
      if (!server_.is_valid() || part_locs_.count() <= 0) {
        bool_ret = false;
      }
      for (int64_t i = 0; true == bool_ret && i < part_locs_.count(); ++i) {
        if (!part_locs_.at(i).is_valid()) {
          bool_ret = false;
        }
      }
      return bool_ret;
    }
    int assign(const ObRangeLocation &location);
    TO_STRING_KV(K_(part_locs), K_(server));
    // Because a task may contain multiple scan operators, each scan operator corresponds to one ObPartLoc
    // So part_locs_ is an array
    common::ModulePageAllocator inner_alloc_;
    common::ObFixedArray<ObTaskInfo::ObPartLoc, common::ObIAllocator> part_locs_;
    common::ObAddr server_;
  };

public:
  explicit ObTaskInfo(common::ObIAllocator &allocator);
  virtual ~ObTaskInfo();
  void set_root_spec(ObOpSpec *root_spec) { root_spec_ = root_spec; }
  ObOpSpec *get_root_spec() const { return root_spec_; }
  // Report execution status to the control node after the remote Task completes
  ObTaskState get_state() const { return state_; }
  void set_state(ObTaskState state) { state_ = state; }
  // Lowest level task for reading data
  inline int set_range_location(const ObTaskInfo::ObRangeLocation &range_loc);
  inline const ObTaskInfo::ObRangeLocation &get_range_location() const;
  inline ObTaskInfo::ObRangeLocation &get_range_location();
  inline void set_task_split_type(int64_t task_split_type) { task_split_type_ = task_split_type; }
  inline int64_t get_task_split_type() { return task_split_type_; }
  // Intermediate result processing task
  void set_task_location(const ObTaskLocation &task_loc) { task_location_ = task_loc; }
  const ObTaskLocation &get_task_location() const { return task_location_; }
  ObTaskLocation &get_task_location() { return task_location_; }
  // Get the data structure required for obtaining the intermediate result
  inline void set_pull_slice_id(uint64_t pull_slice_id) { pull_slice_id_ = pull_slice_id; }
  inline uint64_t get_pull_slice_id() const { return pull_slice_id_; }
  inline void set_force_save_interm_result(bool force_save_interm_result)
  {
    force_save_interm_result_ = force_save_interm_result;
  }
  inline bool is_force_save_interm_result() const { return force_save_interm_result_; }
  // Get location information, index in LocationList
  inline void set_location_idx(uint64_t location_idx) { location_idx_ = location_idx; }
  inline uint64_t get_location_idx() const { return location_idx_; }
  inline int init_location_idx_array(int64_t loc_idx_cnt)
  { return location_idx_list_.init(loc_idx_cnt); }
  inline int add_location_idx(uint64_t location_idx)
  { return location_idx_list_.push_back(location_idx); }
  inline const ObIArray<uint64_t> &get_location_idx_list() const
  { return location_idx_list_; }

  int init_sclie_count_array(int64_t array_count) { return slice_count_pos_.init(array_count); }
  int add_slice_count_pos(int64_t slice_count) { return slice_count_pos_.push_back(slice_count); }

  void set_background(const bool v) { background_ = v; }
  bool is_background() const { return background_; }
  inline const ObTaskID &get_ob_task_id() const { return task_location_.get_ob_task_id(); }
  inline uint64_t get_job_id() const { return task_location_.get_job_id(); }
  inline uint64_t get_task_id() const { return task_location_.get_task_id(); }
  inline void set_task_send_begin(int64_t ts)   { ts_task_send_begin_   = ts; }
  inline void set_task_recv_done(int64_t ts)    { ts_task_recv_done_    = ts; }
  inline void set_result_send_begin(int64_t ts) { ts_result_send_begin_ = ts; }
  inline void set_result_recv_done(int64_t ts)  { ts_result_recv_done_  = ts; }
  inline int64_t get_task_send_begin() const   { return ts_task_send_begin_; }
  inline int64_t get_task_recv_done() const    { return ts_task_recv_done_; }
  inline int64_t get_result_send_begin() const { return ts_result_send_begin_; }
  inline int64_t get_result_recv_done() const  { return ts_result_recv_done_; }
  inline int32_t retry_times() const { return retry_times_; }
  inline void inc_retry_times() { retry_times_++; }

  TO_STRING_KV(N_TASK_LOC, task_location_,
               K_(range_location),
               K_(location_idx),
               K_(location_idx_list),
               K_(state),
               K_(slice_count_pos),
               K_(background),
               K_(retry_times),
               K_(location_idx_list));

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObTaskInfo);

private:
  /* TODO: Group the following members into three categories for better understanding of INFO:
   * 1. TaskInputInfo
   *    > ObRangeLocation range_location_;
   *    > uint64_t pull_slice_id_;
   * 2. TaskActionInfo
   *    > ObPhyOperator *root_op_;
   *    > ObTaskState state_;
   * 3. TaskOutputInfo
   *    > ObTaskLocation task_location_;
   *
   * TaskInputInfo is responsible for recording where the data that this Task consumes comes from
   * TaskActionInfo is responsible for recording how this Task processes the data
   * TaskOutputInfo is responsible for recording where the result of Task data processing is saved
   * The information stored in TaskOutputInfo is very useful for subsequent executing Jobs, it can be used as their input parameters,
   * to construct their TaskInfoInput(ObSliceID)
   */

  /*** Scan physical table Task required data structure ****/
  // Record which Partitions this Task maps to, as well as the server corresponding to these partition & query range
  ObRangeLocation range_location_;
  int64_t task_split_type_;
  // Record the position of intermediate results in this Task (The server in ObTaskLocation also indicates which machine this task should be sent to for execution)
  // Report and provide to upper layer Task for reading
  ObTaskLocation task_location_;

  /*** Read the necessary data structure for Task processing ***/
  // The slice id to pull
  uint64_t pull_slice_id_;
  // The location of the intermediate result is calculated in the ObReceiveInput::init function, therefore it does not need to be saved here
  // Force saving intermediate results remotely, do not bring them back in task event
  bool force_save_interm_result_;
  // task execution report result
  common::ObSEArray<ObSliceEvent, 1> slice_events_;
  // Get location information, index in LocationList
  uint64_t location_idx_;
  ObFixedArray<uint64_t, common::ObIAllocator> location_idx_list_;

  /*** General data structure ***/
  ObPhyOperator *root_op_;
  // The running status of this Task
  ObTaskState state_;

  ObFixedArray<uint64_t, common::ObIAllocator> slice_count_pos_;
  // run task in background threads.
  bool background_;
  // task granularity retry, record retry count, avoid infinite retry
  int32_t retry_times_;
private:
  int64_t ts_task_send_begin_;
  int64_t ts_task_recv_done_;
  int64_t ts_result_send_begin_;
  int64_t ts_result_recv_done_;
  ObOpSpec *root_spec_; // for static engine
};

inline int ObTaskInfo::set_range_location(const ObTaskInfo::ObRangeLocation &range_loc)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(!range_loc.is_valid())) {
    ret = common::OB_INVALID_ARGUMENT;
    SQL_EXE_LOG(WARN, "invalid range location", K(ret), K(range_loc));
  } else {
    ret = range_location_.assign(range_loc);
  }
  return ret;
}

inline const ObTaskInfo::ObRangeLocation &ObTaskInfo::get_range_location() const
{
  return range_location_;
}

inline ObTaskInfo::ObRangeLocation &ObTaskInfo::get_range_location()
{
  return range_location_;
}

}
}
#endif /* OCEANBASE_SQL_EXECUTOR_TASK_INFO_ */
//// end of header file
