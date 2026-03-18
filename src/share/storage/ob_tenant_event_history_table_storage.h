#ifndef OCEANBASE_SHARE_STORAGE_OB_TENANT_EVENT_HISTORY_TABLE_STORAGE_H_
#define OCEANBASE_SHARE_STORAGE_OB_TENANT_EVENT_HISTORY_TABLE_STORAGE_H_

#include "lib/ob_define.h"
#include "lib/container/ob_iarray.h"
#include "lib/net/ob_addr.h"
#include "share/storage/ob_sqlite_connection_pool.h"
#include <functional>

namespace oceanbase
{
namespace share
{

struct ObTenantEventHistoryEntry
{
  uint64_t tenant_id_;
  int64_t gmt_create_;
  common::ObAddr svr_addr_;
  common::ObString module_;
  common::ObString event_;
  common::ObString name1_;
  common::ObString value1_;
  common::ObString name2_;
  common::ObString value2_;
  common::ObString name3_;
  common::ObString value3_;
  common::ObString name4_;
  common::ObString value4_;
  common::ObString name5_;
  common::ObString value5_;
  common::ObString name6_;
  common::ObString value6_;
  common::ObString extra_info_;
  common::ObString trace_id_;
  int64_t cost_time_;
  int64_t ret_code_;
  common::ObString error_msg_;

  ObTenantEventHistoryEntry()
    : tenant_id_(OB_INVALID_TENANT_ID),
      gmt_create_(0),
      svr_addr_(),
      module_(),
      event_(),
      name1_(),
      value1_(),
      name2_(),
      value2_(),
      name3_(),
      value3_(),
      name4_(),
      value4_(),
      name5_(),
      value5_(),
      name6_(),
      value6_(),
      extra_info_(),
      trace_id_(),
      cost_time_(0),
      ret_code_(0),
      error_msg_()
  {}

  void reset() {
    tenant_id_ = OB_INVALID_TENANT_ID;
    gmt_create_ = 0;
    svr_addr_.reset();
    module_.reset();
    event_.reset();
    name1_.reset();
    value1_.reset();
    name2_.reset();
    value2_.reset();
    name3_.reset();
    value3_.reset();
    name4_.reset();
    value4_.reset();
    name5_.reset();
    value5_.reset();
    name6_.reset();
    value6_.reset();
    extra_info_.reset();
    trace_id_.reset();
    cost_time_ = 0;
    ret_code_ = 0;
    error_msg_.reset();
  }
  TO_STRING_EMPTY();
};

class ObTenantEventHistoryTableStorage
{
public:
  ObTenantEventHistoryTableStorage();
  virtual ~ObTenantEventHistoryTableStorage();

  int init(ObSQLiteConnectionPool *pool);
  bool is_inited() const { return nullptr != pool_; }

  int insert(const ObTenantEventHistoryEntry &entry);
  int insert_all(const ObIArray<ObTenantEventHistoryEntry> &entries);
  int delete_expired(int64_t gmt_create_before, int64_t limit);

private:
  int create_table_if_not_exists();

  ObSQLiteConnectionPool *pool_;
  DISALLOW_COPY_AND_ASSIGN(ObTenantEventHistoryTableStorage);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_TENANT_EVENT_HISTORY_TABLE_STORAGE_H_
