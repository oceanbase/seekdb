// Publish namespace-local schema changes to the shared namespace directory.
// SQL parsing and DDL execution stay entirely in the namespace worker.
#include "share/schema/ob_multi_version_schema_service.h"
#include <set>
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
int schema_delta_roundtrip(Frame &request, Frame &reply, int &command_ret) {
  // Namespace DDL may commit from a scheduler thread after the client request
  // has yielded. Such a thread owns no user-session route, so give this
  // boundary command the same short-lived multiplexed route used by other
  // worker background services.
  IndependentStorageScope storage_scope;
  int ret = storage_scope.error();
  if (!ret) { ret = worker_send(request); }
  if (!ret) { ret = worker_read(reply); }
  if (!ret && reply.type() != 'g') { ret = common::OB_INVALID_ARGUMENT; }
  if (!ret) { ret = static_cast<int>(reply.number()); }
  if (!ret) { command_ret = static_cast<int>(reply.number()); ret = reply.ret; }
  return ret;
}
int finish_schema_delta_reply(Frame &reply, int command_ret) {
  return reply.consumed() ? command_ret : common::OB_INVALID_ARGUMENT;
}

int namespace_schema_fence_roundtrip(uint64_t operation, int64_t schema_version = 0) {
  Frame request('M'), reply;
  request.number(operation);
  if (operation == 4 || operation == 6) {
    request.number(schema_version);
  }
  int command_ret = common::OB_SUCCESS;
  int ret = schema_delta_roundtrip(request, reply, command_ret);
  return ret ? ret : finish_schema_delta_reply(reply, command_ret);
}

int begin_namespace_schema_change() {
  return worker_namespace > 1
      ? namespace_schema_fence_roundtrip(2) : common::OB_SUCCESS;
}

int finish_namespace_schema_change(int64_t committed_schema_version) {
  return worker_namespace > 1 && committed_schema_version >= 0
      ? namespace_schema_fence_roundtrip(4, committed_schema_version)
      : worker_namespace <= 1 ? common::OB_SUCCESS : common::OB_INVALID_ARGUMENT;
}

int begin_namespace_schema_recovery(bool &needed) {
  needed = false;
  if (worker_namespace <= 1) { return common::OB_SUCCESS; }
  Frame request('M'), reply;
  request.number(5);
  int command_ret = common::OB_SUCCESS;
  int ret = schema_delta_roundtrip(request, reply, command_ret);
  if (!ret && !command_ret) { needed = reply.number() != 0; }
  return ret ? ret : finish_schema_delta_reply(reply, command_ret);
}

int finish_namespace_schema_recovery(int64_t reconciled_schema_version) {
  return worker_namespace > 1 && reconciled_schema_version > 0
      ? namespace_schema_fence_roundtrip(6, reconciled_schema_version)
      : worker_namespace <= 1 ? common::OB_SUCCESS : common::OB_INVALID_ARGUMENT;
}

int sync_namespace_schema_delta(uint64_t ns, int64_t base_schema_version,
                                int64_t &published_schema_version) {
  using namespace share::schema;
  published_schema_version = base_schema_version;
  auto &service = ObMultiVersionSchemaService::get_instance();
  if (ns == 0) { return common::OB_INVALID_ARGUMENT; }
  // A successful DDL is not complete from the SQL worker's point of view
  // until its private SchemaService can observe the committed metadata.
  // Namespace 1 does not need to publish a fork-directory delta, but it still
  // needs the same local visibility guarantee as every forked namespace.
  if (ns == 1) {
    int ret = service.refresh_and_add_schema(false);
    if (!ret) {
      ret = service.get_runtime_refreshed_schema_version(published_schema_version);
    }
    return ret;
  }
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
  // Send both sides of every changed table.  A table can keep the same
  // table_id while TRUNCATE or repartitioning replaces only its tablets; the
  // storage directory needs the previous schema to remove precisely those
  // old bindings.  This is a schema replacement protocol, not a list of SQL
  // statement kinds.
  std::vector<const ObTableSchema *> upserts;
  std::vector<const ObTableSchema *> previous_schemas;
  for (uint64_t table_id : table_ids) {
    const ObTableSchema *old_schema = nullptr;
    const ObTableSchema *new_schema = nullptr;
    if (OB_FAIL(old_guard.get_table_schema(table_id, old_schema))) {
    } else if (OB_FAIL(new_guard.get_table_schema(table_id, new_schema))) {
      break;
    } else {
      if (new_schema != nullptr) { upserts.push_back(new_schema); }
      if (old_schema != nullptr) { previous_schemas.push_back(old_schema); }
    }
  }
  Frame request('M'), reply;
  if (OB_SUCC(ret)) {
    request.number(3);
    request.number(latest_schema_version);
    request.number(upserts.size());
    for (const ObTableSchema *schema : upserts) { request.append(*schema); }
    request.number(previous_schemas.size());
    for (const ObTableSchema *schema : previous_schemas) { request.append(*schema); }
    if (request.ret != common::OB_SUCCESS) { ret = request.ret; }
  }
  int command_ret = common::OB_SUCCESS;
  if (OB_SUCC(ret)) {
    ret = schema_delta_roundtrip(request, reply, command_ret);
  }
  if (OB_SUCC(ret)) {
    ret = finish_schema_delta_reply(reply, command_ret);
  }
  if (OB_SUCC(ret)) { published_schema_version = latest_schema_version; }
  return ret;
}

int apply_namespace_schema_delta(uint64_t ns, Frame &request) {
  constexpr uint64_t MAX_CHANGED_SCHEMAS = 4096;
  const int64_t schema_version = static_cast<int64_t>(request.number());
  const uint64_t upsert_count = request.number();
  if (request.ret || ns <= 1 || schema_version <= 0
      || upsert_count > MAX_CHANGED_SCHEMAS) {
    return request.ret ? request.ret : common::OB_INVALID_ARGUMENT;
  }
  std::vector<std::unique_ptr<share::schema::ObTableSchema>> holders;
  common::ObArray<const share::schema::ObTableSchema *> upserts;
  common::ObArray<const share::schema::ObTableSchema *> previous_schemas;
  holders.reserve(upsert_count);
  int ret = common::OB_SUCCESS;
  for (uint64_t i = 0; OB_SUCC(ret) && i < upsert_count; ++i) {
    auto schema = std::make_unique<share::schema::ObTableSchema>();
    request.read(*schema);
    if (request.ret) { ret = request.ret; }
    else if (OB_FAIL(upserts.push_back(schema.get()))) {
    } else { holders.push_back(std::move(schema)); }
  }
  const uint64_t previous_count = OB_SUCC(ret) ? request.number() : 0;
  if (OB_SUCC(ret) && (request.ret || previous_count > MAX_CHANGED_SCHEMAS)) {
    ret = request.ret ? request.ret : common::OB_INVALID_ARGUMENT;
  }
  if (OB_SUCC(ret)) { holders.reserve(upsert_count + previous_count); }
  for (uint64_t i = 0; OB_SUCC(ret) && i < previous_count; ++i) {
    auto schema = std::make_unique<share::schema::ObTableSchema>();
    request.read(*schema);
    if (request.ret) { ret = request.ret; }
    else if (OB_FAIL(previous_schemas.push_back(schema.get()))) {
    } else { holders.push_back(std::move(schema)); }
  }
  if (OB_SUCC(ret) && !request.consumed()) { ret = common::OB_INVALID_ARGUMENT; }
  if (OB_SUCC(ret)) {
    ret = storage::NamespaceForkKernelPrototype::publish_schema_delta(
        ns, schema_version, upserts, previous_schemas);
  }
  return ret;
}
} } }
