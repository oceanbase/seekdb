// Publish namespace-local schema changes to the shared namespace directory.
#include "share/schema/ob_multi_version_schema_service.h"
#include <set>
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
int sync_namespace_schema_delta(uint64_t ns, ObMultiVersionSchemaService &service,
                                int64_t base_schema_version,
                                int64_t &published_schema_version) {
  using namespace share::schema;
  published_schema_version = base_schema_version;
  InProcessServingScope serving(ns);
  if (ns == 0) { return common::OB_INVALID_ARGUMENT; }
  // A successful fork DDL must publish its directory delta before returning.
  ObSchemaService *backend = service.get_schema_service();
  common::ObMySQLProxy *proxy = service.get_sql_proxy();
  ObSchemaStatusProxy *status_proxy = service.get_schema_status_proxy();
  if (backend == nullptr || proxy == nullptr || status_proxy == nullptr
      || base_schema_version <= 0) {
    return common::OB_ERR_UNEXPECTED;
  }
  int ret = common::OB_SUCCESS;
  ObRefreshSchemaStatus status;
  int64_t latest_schema_version = common::OB_INVALID_VERSION;
  ObSchemaService::SchemaOperationSetWithAlloc operations;
  ObSchemaGetterGuard old_guard;
  if (OB_FAIL(status_proxy->get_refresh_schema_status(status))) {
  } else if (OB_FAIL(backend->fetch_schema_version(status, *proxy, latest_schema_version))) {
  } else if (latest_schema_version < base_schema_version) {
    return common::OB_SCHEMA_EAGAIN;
  } else if (latest_schema_version == base_schema_version) {
    published_schema_version = latest_schema_version;
    return common::OB_SUCCESS;
  } else if (OB_FAIL(backend->get_increment_schema_operations(
          status, base_schema_version, latest_schema_version, *proxy, operations))) {
  } else if (OB_FAIL(service.get_runtime_schema_guard(old_guard, base_schema_version))) {
  } else if (OB_FAIL(service.refresh_and_add_schema(false))) {
  }
  ObSchemaGetterGuard new_guard;
  if (OB_SUCC(ret)) {
    ret = service.get_runtime_schema_guard(new_guard, latest_schema_version);
  }
  std::set<uint64_t> table_ids;
  for (int64_t i = 0; OB_SUCC(ret) && i < operations.count(); ++i) {
    const ObSchemaOperation &operation = operations.at(i);
    if (operation.op_type_ > OB_DDL_TABLE_OPERATION_BEGIN
        && operation.op_type_ < OB_DDL_TABLE_OPERATION_END
        && operation.table_id_ != common::OB_INVALID_ID
        && !is_inner_table(operation.table_id_)) {
      table_ids.insert(operation.table_id_);
    }
  }
  // A table can keep the same table_id while TRUNCATE or repartitioning
  // replaces only its tablets. The directory needs both schemas to remove
  // precisely those old bindings.
  common::ObArray<const ObTableSchema *> upserts;
  common::ObArray<const ObTableSchema *> previous_schemas;
  for (uint64_t table_id : table_ids) {
    if (ret != common::OB_SUCCESS) { break; }
    const ObTableSchema *old_schema = nullptr;
    const ObTableSchema *new_schema = nullptr;
    if (OB_FAIL(old_guard.get_table_schema(table_id, old_schema))) {
    } else if (OB_FAIL(new_guard.get_table_schema(table_id, new_schema))) {
    } else if (new_schema != nullptr && OB_FAIL(upserts.push_back(new_schema))) {
    } else if (old_schema != nullptr && OB_FAIL(previous_schemas.push_back(old_schema))) {
    }
  }
  if (OB_SUCC(ret) && (upserts.count() > 4096 || previous_schemas.count() > 4096)) {
    ret = common::OB_INVALID_ARGUMENT;
  } else if (OB_SUCC(ret)) {
    ret = storage::NamespaceForkKernelPrototype::publish_schema_delta(
        ns, latest_schema_version, upserts, previous_schemas);
  }
  if (OB_SUCC(ret)) { published_schema_version = latest_schema_version; }
  return ret;
}
} } }
