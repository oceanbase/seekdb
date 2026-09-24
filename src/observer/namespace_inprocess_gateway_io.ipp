// Included inside oceanbase::observer::namespace_worker_prototype.
int call_in_process_direct_insert_simple(RequestTag parent, uint64_t generation,
                                         char operation, bool &is_final)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->direct_insert_registry) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()) { return OB_INVALID_ARGUMENT; }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->direct_insert.simple(space, parent, generation,
      *ctx->direct_insert_registry, operation, is_final);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int resolve_in_process_direct_insert_policy(RequestTag parent, uint64_t generation,
                                            const ObDirectInsertPlanFacts &facts,
                                            ObDirectInsertWritePolicy &policy)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->direct_insert_registry) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()) { return OB_INVALID_ARGUMENT; }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->direct_insert.resolve_policy(space, parent, generation,
      *ctx->direct_insert_registry, facts, policy);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
template <class Call>
int with_in_process_direct_insert(Call &&call)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized || !ctx->direct_insert_registry) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id()) { return OB_INVALID_ARGUMENT; }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = call(ctx->direct_insert, space, *ctx->direct_insert_registry);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int start_in_process_direct_insert(const ObDirectInsertStartParam &param,
    RequestTag &origin, uint64_t &generation)
{
  InProcessStorage *ctx = in_process_storage;
  if (ctx == nullptr || !ctx->initialized || !ctx->direct_insert_registry) { return OB_NOT_INIT; }
  const StorageSpaceHandle space = active_worker_storage_space();
  if (!space.is_namespace() || ctx->ns != space.namespace_id()) { return OB_INVALID_ARGUMENT; }
  if (!ctx->direct_insert_tag.slot) {
    ctx->direct_insert_tag = ctx->direct_insert_registry->acquire();
  }
  if (!ctx->direct_insert_tag.slot) { return OB_EAGAIN; }
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle target, DirectInsertRegistry &registry) {
    return route.start(target, ctx->direct_insert_tag, registry,
        ctx->session_state, param, origin, generation);
  });
}
int build_in_process_direct_insert_autoinc(RequestTag parent, uint64_t generation,
                                           ObDirectInsertAutoincScope scope,
                                           const ObTabletID &tablet, int64_t slice,
                                           ObDirectInsertAutoincParam &param)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.build_autoinc(space, parent, generation, registry,
        scope, tablet, slice, param);
  });
}
int sync_in_process_direct_insert_autoinc(RequestTag parent, uint64_t generation,
                                          const ObTabletID &tablet,
                                          const ObTabletID &target,
                                          int64_t slice, int64_t rows)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.sync_autoinc(space, parent, generation, registry,
        tablet, target, slice, rows);
  });
}
int prepare_in_process_direct_insert_ordered(RequestTag parent, uint64_t generation,
    const ObIArray<ObDDLTabletSliceCount> &slice_counts)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.prepare_ordered(space, parent, generation, registry, slice_counts);
  });
}
int finish_in_process_direct_insert(RequestTag parent, uint64_t generation)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.finish(space, in_process_storage->direct_insert_tag,
        parent, generation, registry);
  });
}
int create_in_process_direct_insert_writer(RequestTag parent, uint64_t generation,
    const ObDirectInsertWriterRequest &request, uint64_t &writer_id)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.create_writer(space, parent, generation, registry, request, writer_id);
  });
}
int control_in_process_direct_insert_writer(RequestTag parent, uint64_t generation,
    uint64_t writer_id, char operation, int64_t &rows)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.control_writer(space, parent, generation, registry,
        writer_id, operation, rows);
  });
}
int append_in_process_direct_insert_writer(RequestTag parent, uint64_t generation,
    uint64_t writer_id, ObDatum *cells, int64_t row_count,
    int64_t column_count, int64_t &rows)
{
  return with_in_process_direct_insert([&](DirectInsertRoute &route,
      StorageSpaceHandle space, DirectInsertRegistry &registry) {
    return route.append_writer(space, parent, generation, registry,
        writer_id, cells, row_count, column_count, rows);
  });
}
int fetch_in_process_scan(uint64_t handle, ScanBatch &batch)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id() || handle == 0) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->scans.fetch(handle, batch);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int close_in_process_scan(uint64_t handle)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id() || handle == 0) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->scans.close(handle);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
int rescan_in_process_scan(uint64_t handle, const ObVTableScanParam &param)
{
  InProcessStorage *ctx = in_process_storage;
  const StorageSpaceHandle space = active_worker_storage_space();
  if (ctx == nullptr || !ctx->initialized) { return OB_NOT_INIT; }
  if (!space.is_namespace() || ctx->ns != space.namespace_id() || handle == 0) {
    return OB_INVALID_ARGUMENT;
  }
  const int64_t old_timeout = THIS_WORKER.get_timeout_ts();
  auto *old_session = THIS_WORKER.get_session();
  THIS_WORKER.set_session(&ctx->session);
  const int ret = ctx->scans.rescan(handle, param);
  THIS_WORKER.set_session(old_session);
  THIS_WORKER.set_timeout_ts(old_timeout);
  return ret;
}
uint64_t in_process_bound_namespace()
{
  return in_process_storage ? in_process_storage->ns : 0;
}
uint64_t in_process_session_ns(sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime != nullptr ? runtime->ns().id() : 0;
}
