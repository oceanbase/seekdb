int worker_storage_space_for_schema(const ObTableSchema &schema,
                                    ObSchemaGetterGuard &guard,
                                    StorageSpaceHandle &storage_space) {
  storage_space = StorageSpaceHandle();
  if (!serves_namespace_schema()
      || NamespaceForkKernelPrototype::is_encoded_id(schema.get_table_id())) {
    return OB_INVALID_ARGUMENT;
  }
  // Native all_* tables are the catalog of the namespace worker itself.
  if (is_inner_table(schema.get_table_id())) {
    storage_space = StorageSpaceHandle::namespace_space(serving_namespace());
    return storage_space.is_valid() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  }
  const ObDatabaseSchema *database = nullptr;
  int ret = guard.get_database_schema(schema.get_database_id(), database);
  if (OB_SUCC(ret) && OB_ISNULL(database)) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (OB_SUCC(ret)) {
    storage_space = uses_global_storage_scope()
        ? StorageSpaceHandle::global_space()
        : StorageSpaceHandle::namespace_space(serving_namespace());
    if (!storage_space.is_valid()) { ret = OB_INVALID_ARGUMENT; }
  }
  return ret;
}
int worker_local_table_schema(uint64_t table_id, int64_t schema_version,
                              ObSchemaGetterGuard &guard, const ObTableSchema *&schema,
                              StorageSpaceHandle &storage_space) {
  schema = nullptr;
  storage_space = StorageSpaceHandle();
  if (!serves_namespace_schema()
      || NamespaceForkKernelPrototype::is_encoded_id(table_id)
      || (!is_inner_table(table_id) && schema_version <= 0)) {
    return OB_INVALID_ARGUMENT;
  }
  // A scan param carries the table schema's own version.  Built-in table
  // versions are compile-time constants (for example 1048712), not a valid
  // multi-version SchemaService snapshot.  Resolve native __all_* definitions
  // from this worker's current pinned cache and keep the requested version only
  // as the storage-schema version below.
  const int64_t guard_version = is_inner_table(table_id)
      ? OB_INVALID_VERSION : schema_version;
  ObMultiVersionSchemaService *service =
      namespace_schema_service(serving_namespace());
  int ret = service == nullptr ? OB_NOT_INIT
      : service->get_runtime_schema_guard(guard, guard_version);
  if (!ret) { ret = guard.get_table_schema(table_id, schema); }
  if (!ret && (schema == nullptr || schema->get_table_id() != table_id)) {
    ret = OB_SCHEMA_EAGAIN;
  }
  if (!ret && is_inner_table(table_id)) {
    // Native __all_* tables always belong to the worker's namespace.  Looking
    // up their database just to rediscover that fact recursively scans
    // __all_database_history through this same remote iterator.
    return worker_storage_space_for_schema(*schema, guard, storage_space);
  }
  if (!ret && schema->get_schema_version() != schema_version) {
    ret = OB_SCHEMA_EAGAIN;
  }
  if (!ret) { ret = worker_storage_space_for_schema(*schema, guard, storage_space); }
  if (ret == OB_TABLE_NOT_EXIST) { schema = nullptr; }
  return ret;
}
