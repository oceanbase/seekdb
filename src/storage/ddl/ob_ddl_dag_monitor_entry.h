#ifndef OCEANBASE_STORAGE_DDL_OB_DDL_DAG_MONITOR_ENTRY_H_
#define OCEANBASE_STORAGE_DDL_OB_DDL_DAG_MONITOR_ENTRY_H_

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
      message_()
  {}
  ~ObDDLDagMonitorEntry() = default;

  common::ObArenaAllocator &get_allocator() { return allocator_; }
  common::ObString get_message() const { return message_; }
  int set_message(const common::ObString &message)
  {
    message_ = message;
    return OB_SUCCESS;
  }
  int set_task_id(const share::ObITask *task)
  {
    UNUSED(task);
    return OB_SUCCESS;
  }
  int set_task_info(const common::ObString &task_info)
  {
    UNUSED(task_info);
    return OB_SUCCESS;
  }
  int set_dag_info(const common::ObString &dag_info)
  {
    UNUSED(dag_info);
    return OB_SUCCESS;
  }

private:
  common::ObArenaAllocator allocator_;
  common::ObString message_;
};

} // namespace storage
} // namespace oceanbase

#endif
