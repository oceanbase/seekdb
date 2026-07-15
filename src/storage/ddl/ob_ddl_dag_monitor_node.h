#ifndef OCEANBASE_STORAGE_DDL_OB_DDL_DAG_MONITOR_NODE_H_
#define OCEANBASE_STORAGE_DDL_OB_DDL_DAG_MONITOR_NODE_H_

#include "lib/allocator/page_arena.h"
#include "share/rc/ob_tenant_base.h"
#include "storage/ddl/ob_ddl_dag_monitor_entry.h"

namespace oceanbase
{
namespace share
{
class ObITask;
}
namespace storage
{

class ObDDLDagMonitorInfo
{
public:
  ObDDLDagMonitorInfo(common::ObIAllocator *allocator, share::ObITask *task)
    : allocator_(allocator),
      task_(task),
      ret_code_(OB_SUCCESS)
  {}
  virtual ~ObDDLDagMonitorInfo() = default;

  virtual int convert_to_monitor_entry(ObDDLDagMonitorEntry &entry) const
  {
    static const common::ObString EMPTY_MSG = common::ObString::make_string("{}");
    int ret = entry.set_task_id(task_);
    if (OB_SUCC(ret)) {
      ret = entry.set_message(EMPTY_MSG);
    }
    return ret;
  }

  void mark_finished() {}
  void set_ret_code(const int ret_code) { ret_code_ = ret_code; }
  int get_ret_code() const { return ret_code_; }
  TO_STRING_KV(K_(ret_code));

protected:
  common::ObIAllocator *allocator_;
  share::ObITask *task_;
  int ret_code_;
};

class ObDDLDagMonitorNode
{
public:
  ObDDLDagMonitorNode()
    : allocator_(ObMemAttr(MTL_ID(), "DDLDagMon"))
  {}
  ~ObDDLDagMonitorNode() = default;

  template <typename T>
  int alloc_monitor_info(share::ObITask *task, T *&info)
  {
    int ret = OB_SUCCESS;
    void *buf = allocator_.alloc(sizeof(T));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      info = new (buf) T(&allocator_, task);
    }
    return ret;
  }

private:
  common::ObArenaAllocator allocator_;
};

} // namespace storage
} // namespace oceanbase

#endif
