// Native range sizing/splitting stays with the tablets in shared storage.
#include "data_plane/ob_i_range_service.h"
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
using namespace data_plane;

void append_range(Frame &frame, const ObStoreRange &range) {
  frame.number(range.get_table_id()); frame.number(range.get_border_flag().get_data());
  for (const auto *key : {&range.get_start_key(), &range.get_end_key()}) {
    frame.number(key->get_obj_cnt());
    for (int64_t i = 0; i < key->get_obj_cnt(); ++i) { frame.append(key->get_obj_ptr()[i]); }
  }
}
int read_range(Frame &frame, ObIAllocator &allocator, ObStoreRange &range) {
  range.set_table_id(frame.number());
  ObBorderFlag border; border.set_data(frame.number()); range.set_border_flag(border);
  for (int side = 0; side < 2; ++side) {
    const uint64_t count = frame.number();
    if (frame.ret || !count || count > OB_MAX_ROWKEY_COLUMN_NUMBER) { return OB_INVALID_ARGUMENT; }
    auto *cells = static_cast<ObObj *>(allocator.alloc(count * sizeof(ObObj)));
    if (!cells) { return OB_ALLOCATE_MEMORY_FAILED; }
    for (uint64_t i = 0; i < count; ++i) {
      new (&cells[i]) ObObj();
      ObObj cell; frame.read(cell);
      int ret = frame.ret ? frame.ret : ob_write_obj(allocator, cell, cells[i]);
      if (ret) { return ret; }
    }
    ObStoreRowkey key; key.assign(cells, count);
    if (!side) { range.set_start_key(key); } else { range.set_end_key(key); }
  }
  return OB_SUCCESS;
}
int process_ranges(StorageSpaceHandle channel_space, Frame &request, Frame &reply) {
  const uint64_t operation = request.number();
  const ObTabletID logical_tablet(request.number());
  const bool has_logical_schema = request.number() != 0;
  StorageSpaceHandle storage_space;
  int ret = read_storage_space(request, channel_space, storage_space);
  const bool namespace_local = storage_space.is_namespace();
  const uint64_t ns = storage_space.namespace_id();
  ObArenaAllocator allocator(ObMemAttr("NsRanges"));
  ObTableSchema logical_schema(&allocator);
  ObTableSchema storage_schema(&allocator);
  if (has_logical_schema) { request.read(logical_schema); }
  const uint64_t materialization_schema_count = request.number();
  std::vector<std::unique_ptr<ObTableSchema>> logical_materialization_schemas;
  std::vector<std::unique_ptr<ObTableSchema>> routed_materialization_schemas;
  ObArray<const ObTableSchema *> materialization_schemas;
  if (materialization_schema_count > 3
      || (!namespace_local && materialization_schema_count != 0)) {
    ret = OB_INVALID_ARGUMENT;
  }
  for (uint64_t i = 0; OB_SUCC(ret) && i < materialization_schema_count; ++i) {
    auto logical = std::make_unique<ObTableSchema>(&allocator);
    request.read(*logical);
    if (request.ret) {
      ret = request.ret;
    } else {
      auto routed = std::make_unique<ObTableSchema>(&allocator);
      int schema_ret = ns <= 1
          ? routed->assign(*logical)
          : NamespaceForkKernelPrototype::make_storage_schema(ns, *logical, *routed);
      if (schema_ret != OB_SUCCESS) {
        ret = schema_ret;
      } else if (OB_FAIL(materialization_schemas.push_back(routed.get()))) {
      } else {
        logical_materialization_schemas.push_back(std::move(logical));
        routed_materialization_schemas.push_back(std::move(routed));
      }
    }
  }
  const int64_t deadline = request.number();
  const int64_t tasks = request.number();
  const uint64_t count = request.number();
  if (OB_SUCC(ret) && request.ret) { ret = request.ret; }
  const uint64_t logical_table_id = has_logical_schema
      ? logical_schema.get_table_id() : OB_INVALID_ID;
  uint64_t storage_table_id = logical_table_id;
  ObTabletID tablet = logical_tablet;
  const ObTableSchema *effective_schema = has_logical_schema
      ? &logical_schema : nullptr;
  if (OB_SUCC(ret) && namespace_local && ns > 1) {
    if (!has_logical_schema) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(NamespaceForkKernelPrototype::make_storage_schema(
                   ns, logical_schema, storage_schema))) {
    } else if (OB_FAIL(NamespaceForkKernelPrototype::storage_object_id(
                   ns, logical_table_id, storage_table_id))) {
    } else if (OB_FAIL(route_tablet_id(storage_space, tablet))) {
    } else {
      effective_schema = &storage_schema;
    }
  }
  ObSEArray<ObStoreRange, 4> ranges;
  if (!ret && (!storage_space.is_valid() || !tablet.is_valid()
      || !count || count > MAX_FRAME / 32
      || (operation != 'C' && operation != 'S') || (operation == 'S' && tasks <= 0))) { ret = OB_INVALID_ARGUMENT; }
  for (uint64_t i = 0; !ret && i < count; ++i) {
    ObStoreRange range; ret = read_range(request, allocator, range);
    if (!ret && has_logical_schema && range.get_table_id() != logical_table_id) {
      ret = OB_INVALID_ARGUMENT;
    }
    if (!ret && has_logical_schema) { range.set_table_id(storage_table_id); }
    if (!ret) { ret = ranges.push_back(range); }
  }
  if (!ret && !request.consumed()) { ret = OB_INVALID_ARGUMENT; }
  if (!ret && !materialization_schemas.empty()) {
    ret = NamespaceForkKernelPrototype::ensure_tablet(
        tablet, *effective_schema, materialization_schemas);
  }
  const int64_t remaining = std::min(deadline, THIS_WORKER.get_timeout_ts()) - ObTimeUtility::current_time();
  if (!ret && remaining <= 0) { ret = OB_TIMEOUT; }
  int64_t size = 0;
  ObArrayArray<ObStoreRange> split;
  auto *service = share::server_service<ObIRangeService>();
  if (!ret && !service) { ret = OB_NOT_INIT; }
  if (!ret && operation == 'C') { ret = service->get_multi_ranges_cost(tablet, remaining, ranges, size); }
  else if (!ret) { ret = service->split_multi_ranges(tablet, remaining, ranges, tasks, allocator, split); }
  reply = Frame('g'); reply.number(ret);
  if (!ret && operation == 'C') { reply.number(size); }
  else if (!ret) {
    reply.number(split.count());
    for (int64_t i = 0; i < split.count(); ++i) {
      reply.number(split.count(i));
      for (int64_t j = 0; j < split.count(i); ++j) {
        ObStoreRange logical_range = split.at(i, j);
        if (has_logical_schema) { logical_range.set_table_id(logical_table_id); }
        append_range(reply, logical_range);
      }
    }
  }
  return reply.ret;
}
class RemoteRangeService final : public ObIRangeService {
public:
  int call(char operation, const ObTabletID &tablet, int64_t timeout,
      const ObIArray<ObStoreRange> &ranges, int64_t tasks, Frame &reply) {
    if (timeout <= 0) { return OB_TIMEOUT; }
    if (ranges.empty()) { return OB_INVALID_ARGUMENT; }
    StorageSessionScope scope(THIS_WORKER.get_session());
    int ret = scope.error();
    SessionBinding *temporary = nullptr;
    if (!ret && !worker_request) { ret = begin_direct_request(0, temporary, true); }
    const uint64_t table_id = ranges.at(0).get_table_id();
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *logical_schema = nullptr;
    StorageSpaceHandle storage_space = active_worker_storage_space();
    bool send_logical_schema = owns_namespace_schema()
        && !NamespaceForkKernelPrototype::is_encoded_id(table_id)
        && (!is_inner_table(table_id) || worker_namespace > 1);
    if (!ret && send_logical_schema) {
      ret = ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
          schema_guard);
      if (!ret) { ret = schema_guard.get_table_schema(table_id, logical_schema); }
      if (!ret && (logical_schema == nullptr
          || logical_schema->get_table_id() != table_id)) {
        ret = OB_SCHEMA_EAGAIN;
      }
      if (!ret) {
        ret = worker_storage_space_for_schema(
            *logical_schema, schema_guard, storage_space);
      }
    }
    ObArray<const ObTableSchema *> materialization_schemas;
    if (!ret && send_logical_schema && storage_space.is_namespace()
        && worker_namespace > 1) {
      ret = worker_materialization_schemas(
          *logical_schema, schema_guard, materialization_schemas);
    }
    for (int64_t i = 1; !ret && i < ranges.count(); ++i) {
      if (ranges.at(i).get_table_id() != table_id) { ret = OB_INVALID_ARGUMENT; }
    }
    Frame request('G'); request.number(operation); request.number(tablet.id());
    request.number(send_logical_schema);
    write_storage_space(request, storage_space);
    if (send_logical_schema) { request.append(*logical_schema); }
    request.number(materialization_schemas.count());
    for (const ObTableSchema *schema : materialization_schemas) {
      request.append(*schema);
    }
    const int64_t now = ObTimeUtility::current_time();
    request.number(timeout > INT64_MAX - now ? INT64_MAX : now + timeout);
    request.number(tasks); request.number(ranges.count());
    for (int64_t i = 0; i < ranges.count(); ++i) { append_range(request, ranges.at(i)); }
    if (!ret) { ret = request.ret ? request.ret : worker_send(request); }
    if (!ret) { ret = worker_read(reply); }
    if (!ret) { ret = reply.type() == 'g' ? static_cast<int>(reply.number()) : OB_INVALID_ARGUMENT; }
    close_session(temporary);
    return ret ? ret : reply.ret;
  }
  int get_multi_ranges_cost(const ObTabletID &tablet, int64_t timeout,
      const ObIArray<ObStoreRange> &ranges, int64_t &size) override {
    Frame reply; int ret = call('C', tablet, timeout, ranges, 0, reply);
    if (!ret) { size = reply.number(); if (!reply.consumed()) { ret = OB_INVALID_ARGUMENT; } }
    return ret;
  }
  int split_multi_ranges(const ObTabletID &tablet, int64_t timeout,
      const ObIArray<ObStoreRange> &ranges, int64_t tasks, ObIAllocator &allocator,
      ObArrayArray<ObStoreRange> &split) override {
    Frame reply; int ret = call('S', tablet, timeout, ranges, tasks, reply);
    const uint64_t groups = ret ? 0 : reply.number();
    if (!ret && (reply.ret || groups > MAX_FRAME / 8)) { ret = OB_INVALID_ARGUMENT; }
    split.reset();
    for (uint64_t i = 0; !ret && i < groups; ++i) {
      ObSEArray<ObStoreRange, 4> group;
      const uint64_t count = reply.number();
      if (reply.ret || count > MAX_FRAME / 32) { ret = OB_INVALID_ARGUMENT; }
      for (uint64_t j = 0; !ret && j < count; ++j) {
        ObStoreRange range; ret = read_range(reply, allocator, range);
        if (!ret) { ret = group.push_back(range); }
      }
      if (!ret) { ret = split.push_back(group); }
    }
    if (!ret && !reply.consumed()) { ret = OB_INVALID_ARGUMENT; }
    return ret;
  }
};
} } }
