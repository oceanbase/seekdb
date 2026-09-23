#ifndef OCEANBASE_ROOTSERVER_OB_DDL_AUTOINCREMENT_SERVICE_H_
#define OCEANBASE_ROOTSERVER_OB_DDL_AUTOINCREMENT_SERVICE_H_

#include "namespace/namespace.h"
#include "share/ob_autoincrement_service.h"

namespace oceanbase { namespace rootserver {

inline int ddl_autoincrement_service(const common::ObMySQLProxy &sql_proxy,
                                    share::ObAutoincrementService *&service)
{
  service = nullptr;
  const uint64_t namespace_id = sql_proxy.target_namespace();
  if (namespace_id <= 1) {
    service = &share::ObAutoincrementService::get_instance();
  } else {
    ns::NamespaceRuntime *runtime = nullptr;
    if (ns::namespace_registry().get(namespace_id, runtime) && runtime != nullptr) {
      service = static_cast<share::ObAutoincrementService *>(
          runtime->service(ns::NamespaceRuntime::AUTOINCREMENT_SERVICE));
    }
  }
  return service != nullptr ? common::OB_SUCCESS : common::OB_NOT_INIT;
}

}} // namespace oceanbase::rootserver

#endif
