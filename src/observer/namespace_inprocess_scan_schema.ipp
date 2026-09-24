int worker_storage_space_for_schema(const ObTableSchema &schema,
                                    ObSchemaGetterGuard &guard,
                                    StorageSpaceHandle &storage_space) {
  storage_space = StorageSpaceHandle();
  if (!serves_namespace_schema()
      || NamespaceForkKernelPrototype::is_encoded_id(schema.get_table_id())) {
    return OB_INVALID_ARGUMENT;
  }
  // Native all_* tables are the catalog of the namespace worker itself. A
  // global user table still records its schema in namespace 1's catalog; only
  // the target table's storage belongs to GLOBAL.
  if (is_inner_table(schema.get_table_id())) {
    storage_space = StorageSpaceHandle::namespace_space(serving_namespace());
    return storage_space.is_valid() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  }
  const ObDatabaseSchema *database = nullptr;
  int ret = guard.get_database_schema(schema.get_database_id(), database);
  if (OB_SUCC(ret) && OB_ISNULL(database)) {
    ret = OB_ERR_UNEXPECTED;
  }
  const bool control_database = OB_SUCC(ret)
      && is_namespace_control_database(database->get_database_name_str());
  if (OB_SUCC(ret) && control_database
      && !can_access_namespace_control_database()) {
    ret = OB_TABLE_NOT_EXIST;
  }
  if (OB_SUCC(ret)) {
    storage_space = uses_global_storage_scope() || control_database
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
  ObMultiVersionSchemaService *service = is_inner_table(table_id)
      ? &ObMultiVersionSchemaService::get_instance()
      : namespace_schema_service(serving_namespace());
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
int worker_materialization_schemas(
    const ObTableSchema &requested,
    ObSchemaGetterGuard &guard,
    ObIArray<const ObTableSchema *> &schemas) {
  schemas.reset();
  const ObTableSchema *main = &requested;
  int ret = OB_SUCCESS;
  if (requested.is_aux_lob_table()) {
    ret = guard.get_table_schema(requested.get_data_table_id(), main);
    if (!ret && (main == nullptr || main->is_aux_lob_table())) {
      ret = OB_SCHEMA_EAGAIN;
    }
  }
  if (!ret) { ret = schemas.push_back(main); }
  const uint64_t auxiliary_ids[] = {
      !ret ? main->get_aux_lob_meta_tid() : OB_INVALID_ID,
      !ret ? main->get_aux_lob_piece_tid() : OB_INVALID_ID};
  for (uint64_t id : auxiliary_ids) {
    if (!ret && id != OB_INVALID_ID) {
      const ObTableSchema *auxiliary = nullptr;
      if (OB_FAIL(guard.get_table_schema(id, auxiliary))) {
      } else if (auxiliary == nullptr || !auxiliary->is_aux_lob_table()
          || auxiliary->get_data_table_id() != main->get_table_id()) {
        ret = OB_SCHEMA_EAGAIN;
      } else {
        ret = schemas.push_back(auxiliary);
      }
    }
  }
  return ret;
}
struct ScanSchema {
  ObArenaAllocator alloc{ObMemAttr("NsScanSchema")};
  ObTableSchema *logical = nullptr;
  ObTableSchema *routed = nullptr;
  ObArray<ObTabletID> logical_tablets;
  ObArray<ObTabletID> storage_tablets;
};
struct ScanBatch {
  ObArenaAllocator allocator{ObMemAttr("NsScanBatch")};
  std::vector<ObObj> cells;
  uint64_t rows = 0;
  size_t bytes = 25; // Former batch header plus result, end, and row count.
  bool end = false;
  void reset() {
    cells.clear();
    allocator.reset();
    rows = 0;
    bytes = 25;
    end = false;
  }
  int push(const ObObj &value, bool has_lob_header) {
    const int64_t encoded_size = value.get_serialize_size();
    if (encoded_size < 0 || bytes > MAX_SQL_MESSAGE - 8
        || static_cast<size_t>(encoded_size) > MAX_SQL_MESSAGE - bytes - 8) {
      return OB_SIZE_OVERFLOW;
    }
    ObObj copied;
    int ret = ob_write_obj(allocator, value, copied);
    if (!ret) {
      if (has_lob_header) { copied.set_has_lob_header(); }
      cells.push_back(copied);
      bytes += static_cast<size_t>(encoded_size) + 8;
    }
    return ret;
  }
};
int fetch_in_process_scan(uint64_t handle, ScanBatch &batch);
int close_in_process_scan(uint64_t handle);
int rescan_in_process_scan(uint64_t handle, const ObVTableScanParam &param);
int open_in_process_scan(StorageSpaceHandle storage_space,
                         const ObVTableScanParam &param,
                         const ObTableSchema &logical_schema, uint64_t &handle);
int copy_scan_schema(uint64_t ns, bool namespace_local, const ObTableSchema &source,
                     std::unique_ptr<ScanSchema> &entry) {
  const int64_t size = source.get_serialize_size();
  if (size <= 0 || size > 4 * 1024 * 1024) { return OB_INVALID_ARGUMENT; }
  auto built = std::make_unique<ScanSchema>();
  void *buf = built->alloc.alloc(sizeof(ObTableSchema));
  if (!buf) { return OB_ALLOCATE_MEMORY_FAILED; }
  built->logical = new (buf) ObTableSchema(&built->alloc);
  int ret = built->logical->assign(source);
  if (ret) { return ret; }
  const bool has_physical_tablets = !is_virtual_table(source.get_table_id());
  if (has_physical_tablets
      && OB_FAIL(built->logical->get_tablet_ids(built->logical_tablets))) { return ret; }
  if (namespace_local && has_physical_tablets) {
    buf = built->alloc.alloc(sizeof(ObTableSchema));
    if (!buf) { return OB_ALLOCATE_MEMORY_FAILED; }
    built->routed = new (buf) ObTableSchema(&built->alloc);
    if (OB_FAIL(NamespaceForkKernelPrototype::make_storage_schema(
            ns, *built->logical, *built->routed))) { return ret; }
  } else {
    built->routed = built->logical;
  }
  if (has_physical_tablets
      && OB_FAIL(built->routed->get_tablet_ids(built->storage_tablets))) { return ret; }
  entry = std::move(built);
  return OB_SUCCESS;
}
