#ifndef OCEANBASE_STORAGE_DDL_OB_DDL_DAG_MONITOR_NODE_H_
#define OCEANBASE_STORAGE_DDL_OB_DDL_DAG_MONITOR_NODE_H_

#include "lib/allocator/page_arena.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_print_utils.h"
#include "observer/scheduler/ob_tenant_dag_scheduler.h"
#include "share/rc/ob_tenant_base.h"
#include "storage/ddl/ob_ddl_dag_monitor_entry.h"

namespace oceanbase
{
namespace storage
{

class ObDDLDagMonitorInfo
{
public:
  ObDDLDagMonitorInfo(common::ObIAllocator *allocator, share::ObITask *task)
    : allocator_(allocator),
      task_(task),
      create_ts_(common::ObTimeUtility::current_time()),
      finish_ts_(0),
      schedule_count_(1),
      execution_time_us_(0),
      ret_code_(OB_SUCCESS)
  {}
  virtual ~ObDDLDagMonitorInfo() = default;

  virtual int convert_to_monitor_entry(ObDDLDagMonitorEntry &entry) const
  {
    int ret = OB_SUCCESS;
    const char *task_type_name = "UNKNOWN";
    int64_t task_type = -1;
    const int64_t finish_ts = finish_ts_;
    const int64_t execution_time_us = execution_time_us_ > 0
        ? execution_time_us_
        : (finish_ts > create_ts_ ? finish_ts - create_ts_ : 0);
    char *buf = nullptr;
    const int64_t buf_len = 512;
    int64_t pos = 0;
    if (OB_NOT_NULL(task_)) {
      task_type = static_cast<int64_t>(task_->get_type());
      if (task_type >= 0
          && task_type < share::ObITask::TASK_TYPE_MAX
          && nullptr != share::ObITask::ObITaskTypeStr[task_type]) {
        task_type_name = share::ObITask::ObITaskTypeStr[task_type];
      }
    }
    if (OB_FAIL(entry.set_task_id(task_))) {
    } else if (OB_FAIL(entry.set_task_info(common::ObString::make_string(task_type_name)))) {
    } else if (OB_ISNULL(buf = static_cast<char *>(entry.get_allocator().alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(databuff_printf(buf,
                                       buf_len,
                                       pos,
                                       "{\"task_type\":\"%s\",\"task_type_id\":%ld,\"ret_code\":%d,"
                                       "\"create_time_us\":%ld,\"finish_time_us\":%ld,"
                                       "\"schedule_count\":%ld,\"execution_time_us\":%ld,\"is_finished\":%d}",
                                       task_type_name,
                                       task_type,
                                       ret_code_,
                                       create_ts_,
                                       finish_ts,
                                       schedule_count_,
                                       execution_time_us,
                                       finish_ts > 0 ? 1 : 0))) {
    } else {
      ret = entry.set_message(common::ObString(static_cast<int32_t>(pos), buf));
    }
    return ret;
  }

  void mark_finished()
  {
    if (finish_ts_ <= 0) {
      finish_ts_ = common::ObTimeUtility::current_time();
      execution_time_us_ = finish_ts_ > create_ts_ ? finish_ts_ - create_ts_ : 0;
    }
  }
  void set_ret_code(const int ret_code) { ret_code_ = ret_code; }
  int get_ret_code() const { return ret_code_; }
  TO_STRING_KV(K_(create_ts), K_(finish_ts), K_(schedule_count), K_(execution_time_us), K_(ret_code));

protected:
  common::ObIAllocator *allocator_;
  share::ObITask *task_;
  int64_t create_ts_;
  int64_t finish_ts_;
  int64_t schedule_count_;
  int64_t execution_time_us_;
  int ret_code_;
};

class ObDDLDagMonitorNode
{
public:
  ObDDLDagMonitorNode()
    : allocator_(ObMemAttr(MTL_ID(), "DDLDagMon")),
      create_ts_(common::ObTimeUtility::current_time()),
      finish_ts_(0),
      monitor_info_cnt_(0)
  {}
  ~ObDDLDagMonitorNode() = default;

  int64_t get_create_ts() const { return create_ts_; }
  int64_t get_finish_ts() const { return finish_ts_; }
  int64_t get_monitor_info_cnt() const { return monitor_info_cnt_; }
  void mark_finished()
  {
    if (finish_ts_ <= 0) {
      finish_ts_ = common::ObTimeUtility::current_time();
    }
  }

  template <typename T>
  int alloc_monitor_info(share::ObITask *task, T *&info)
  {
    int ret = OB_SUCCESS;
    void *buf = allocator_.alloc(sizeof(T));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      info = new (buf) T(&allocator_, task);
      ++monitor_info_cnt_;
    }
    return ret;
  }

private:
  common::ObArenaAllocator allocator_;
  int64_t create_ts_;
  int64_t finish_ts_;
  int64_t monitor_info_cnt_;
};

} // namespace storage
} // namespace oceanbase

#endif
