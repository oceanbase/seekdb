#ifndef OCEANBASE_STORAGE_DDL_OB_DDL_DAG_MONITOR_ENTRY_H_
#define OCEANBASE_STORAGE_DDL_OB_DDL_DAG_MONITOR_ENTRY_H_

#include <cstdint>

#include "lib/allocator/page_arena.h"
#include "lib/string/ob_string.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
namespace share
{
class ObITask;
}
namespace storage
{

class ObDDLDagMonitorEntry
{
public:
  ObDDLDagMonitorEntry()
    : allocator_(ObMemAttr(MTL_ID(), "DDLDagMonEnt")),
      task_addr_(0),
      task_info_(),
      dag_info_(),
      message_()
  {}
  ~ObDDLDagMonitorEntry() = default;

  common::ObArenaAllocator &get_allocator() { return allocator_; }
  int64_t get_task_addr() const { return task_addr_; }
  common::ObString get_task_info() const { return task_info_; }
  common::ObString get_dag_info() const { return dag_info_; }
  common::ObString get_message() const { return message_; }
  int set_message(const common::ObString &message)
  {
    return copy_string_(message, message_);
  }
  int set_task_id(const share::ObITask *task)
  {
    task_addr_ = static_cast<int64_t>(reinterpret_cast<intptr_t>(task));
    return OB_SUCCESS;
  }
  int set_task_info(const common::ObString &task_info)
  {
    return copy_string_(task_info, task_info_);
  }
  int set_dag_info(const common::ObString &dag_info)
  {
    return copy_string_(dag_info, dag_info_);
  }

private:
  int copy_string_(const common::ObString &src, common::ObString &dst)
  {
    int ret = OB_SUCCESS;
    if (src.empty()) {
      dst.reset();
    } else {
      char *buf = static_cast<char *>(allocator_.alloc(src.length()));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(buf, src.ptr(), src.length());
        dst.assign_ptr(buf, static_cast<int32_t>(src.length()));
      }
    }
    return ret;
  }

private:
  common::ObArenaAllocator allocator_;
  int64_t task_addr_;
  common::ObString task_info_;
  common::ObString dag_info_;
  common::ObString message_;
};

} // namespace storage
} // namespace oceanbase

#endif
