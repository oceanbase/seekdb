// V18: a native ObISQLClient result streamed through the existing IPC pump.
#include "observer/namespace_worker_inner_sql_prototype.h"
#include "observer/ob_inner_sql_connection.h"
#include "observer/ob_inner_sql_result.h"
#include "common/mysqlclient/ob_isql_result_handler.h"
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
Frame inner_request(SessionBinding &binding, ObInnerSQLConnection &connection, Frame payload, int64_t &deadline) {
  int64_t timeout = 0;
  connection.get_session().get_query_timeout(timeout);
  deadline = std::min(THIS_WORKER.get_timeout_ts(), ObTimeUtility::current_time() + timeout);
  Frame request('I', MAX_SQL_MESSAGE); request.number(binding.slot); request.number(binding.slot_generation);
  request.number(deadline);
  request.number(GCTX.in_bootstrap_);
  request.append(connection.get_session().get_ddl_info());
  request.data.insert(request.data.end(), payload.data.begin() + Frame::HEADER_SIZE, payload.data.end());
  if (!request.ret) { request.ret = payload.ret; }
  if (request.data.size() > MAX_SQL_MESSAGE) { request.ret = OB_SIZE_OVERFLOW; }
  return request;
}
struct InnerExchange {
  SessionBinding &binding;
  ReadScans scans;
  Exchange pump;
  InnerExchange(SessionBinding &b, Frame request, int64_t deadline)
      : binding(b), scans(b.channel->storage_space),
        pump(*b.channel, std::move(request), &scans,
                                    deadline, b.writes.get()) {}
  int next(Frame &frame) {
    int ret = pump.next(frame);
    if (!ret && frame.type() == 'D') {
      ret = static_cast<int>(frame.number());
      int state_ret = apply_session_state(*binding.gateway, frame);
      if (!ret) { ret = state_ret; }
      if (!ret && !frame.consumed()) { ret = OB_INVALID_ARGUMENT; }
      scans.scans.clear(); binding.gateway->reset_reserved_snapshot_version();
      if (binding.writes->check_finished()) { binding.channel->fail(); ret = OB_ERR_UNEXPECTED; }
      if (!ret) { ret = pump.cancelled ? pump.cancelled.load() : OB_ITER_END; }
    }
    return ret;
  }
  int close() {
    if (pump.finished) { return OB_SUCCESS; }
    int ret = pump.cancel(OB_ERR_QUERY_INTERRUPTED);
    Frame frame;
    while (!ret) { ret = next(frame); }
    return ret == OB_ITER_END || ret == OB_ERR_QUERY_INTERRUPTED ? OB_SUCCESS : ret;
  }
  ~InnerExchange() { close(); }
};
class InnerResult final : public common::sqlclient::ObMySQLResult,
                          public common::sqlclient::ObISQLResultHandler {
public:
  common::sqlclient::ObISQLConnectionGuard connection;
  std::unique_ptr<InnerExchange> stream;
  std::vector<std::string> names;
  std::vector<ObObj> cells;
  mutable ObArenaAllocator converted{ObMemAttr("NsInnerValue")};
  Frame frame;
  ObNewRow row;
  bool positioned = false;
  InnerResult(ObInnerSQLConnection &conn, SessionBinding &binding, Frame &request, int64_t deadline)
      : connection(conn.get_shared_guard()), stream(new InnerExchange(binding, std::move(request), deadline)) {}
  ~InnerResult() override { close(); }
  common::sqlclient::ObMySQLResult *mysql_result() override { return this; }
  int init() {
    int ret = stream->next(frame);
    if (ret) { return ret; }
    if (frame.type() != 'm') { return OB_INVALID_ARGUMENT; }
    const uint64_t count = frame.number();
    if (!count || count > OB_MAX_COLUMN_NUMBER) { return OB_SIZE_OVERFLOW; }
    for (uint64_t i = 0; i < count; ++i) { auto name = frame.string(); names.emplace_back(name.ptr(), name.length()); }
    cells.resize(count);
    return frame.consumed() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  }
  int close() override { positioned = false; int ret = stream ? stream->close() : OB_SUCCESS; stream.reset(); return ret; }
  int next() override {
    positioned = false;
    converted.reuse();
    if (!stream) { return OB_ITER_END; }
    int ret = stream->next(frame);
    if (ret) { return ret; }
    if (frame.type() != 'r' || frame.number() != cells.size()) { return OB_INVALID_ARGUMENT; }
    for (auto &cell : cells) { frame.read_object(cell); }
    if (!frame.consumed()) { return OB_INVALID_ARGUMENT; }
    row.cells_ = cells.data(); row.count_ = cells.size(); positioned = true; return OB_SUCCESS;
  }
  int64_t get_column_count() const override { return names.size(); }
  const ObNewRow *get_row() const override { return positioned ? &row : nullptr; }
  int index(const char *name, int64_t &idx) const {
    if (!name) { return OB_INVALID_ARGUMENT; }
    for (size_t i = 0; i < names.size(); ++i) { if (names[i] == name) { idx = i; return OB_SUCCESS; } }
    return OB_ERR_COLUMN_NOT_FOUND;
  }
  int get_obj(int64_t idx, ObObj &obj, const ObTimeZoneInfo * = nullptr, ObIAllocator * = nullptr) const override {
    if (!positioned) { return OB_NOT_INIT; }
    if (idx < 0 || idx >= int64_t(cells.size())) { return OB_SIZE_OVERFLOW; }
    obj = cells[idx]; return OB_SUCCESS;
  }
  int get_obj(const char *name, ObObj &obj) const override { int64_t idx; int ret = index(name, idx); return ret ? ret : get_obj(idx, obj); }
#define INNER_VALUE(method, native, Type) \
  int method(int64_t idx, Type &value) const override { \
    ObObj obj; int ret = get_obj(idx, obj); \
    return ret ? ret : obj.is_null() ? OB_ERR_NULL_VALUE : obj.native(value); } \
  int method(const char *name, Type &value) const override { \
    int64_t idx; int ret = index(name, idx); return ret ? ret : method(idx, value); }
  INNER_VALUE(get_uint, get_uint64, uint64_t)
  INNER_VALUE(get_datetime, get_datetime, int64_t)
  INNER_VALUE(get_date, get_date, int32_t)
  INNER_VALUE(get_time, get_time, int64_t)
  INNER_VALUE(get_year, get_year, uint8_t)
  INNER_VALUE(get_varchar, get_varchar, ObString)
  INNER_VALUE(get_float, get_float, float)
  INNER_VALUE(get_double, get_double, double)
#undef INNER_VALUE
  int get_number(int64_t idx, common::number::ObNumber &value) const override {
    ObObj obj; int ret = get_obj(idx, obj);
    if (!ret && obj.is_null()) { ret = OB_ERR_NULL_VALUE; }
    if (!ret) {
      ret = obj.is_decimal_int()
          ? wide::to_number(obj.get_decimal_int(), obj.get_int_bytes(), obj.get_scale(), converted, value)
          : obj.get_number(value);
    }
    return ret;
  }
  int get_number(const char *name, common::number::ObNumber &value) const override {
    int64_t idx; int ret = index(name, idx); return ret ? ret : get_number(idx, value);
  }
  int get_int(int64_t idx, int64_t &value) const override {
    ObObj obj; int ret = get_obj(idx, obj);
    if (!ret && obj.is_null()) { ret = OB_ERR_NULL_VALUE; }
    if (!ret && obj.get_type_class() != ObIntTC) { ret = OB_OBJ_TYPE_ERROR; }
    if (!ret) { value = obj.get_int(); }
    return ret;
  }
  int get_int(const char *name, int64_t &value) const override {
    int64_t idx; int ret = index(name, idx); return ret ? ret : get_int(idx, value);
  }
  int get_bool(int64_t idx, bool &value) const override { int64_t n; int ret = get_int(idx, n); if (!ret) { value = n != 0; } return ret; }
  int get_bool(const char *name, bool &value) const override { int64_t idx; int ret = index(name, idx); return ret ? ret : get_bool(idx, value); }
  int get_type(int64_t idx, ObObjMeta &type) const override { ObObj obj; int ret = get_obj(idx, obj); if (!ret) { type = obj.get_meta(); } return ret; }
  int get_type(const char *name, ObObjMeta &type) const override { int64_t idx; int ret = index(name, idx); return ret ? ret : get_type(idx, type); }
  int get_timestamp(int64_t idx, const ObTimeZoneInfo *, int64_t &value) const override {
    ObObj obj; int ret = get_obj(idx, obj); if (!ret && obj.is_null()) { ret = OB_ERR_NULL_VALUE; }
    if (!ret) { value = obj.get_timestamp(); } return ret;
  }
  int get_timestamp(const char *name, const ObTimeZoneInfo *tz, int64_t &value) const override {
    int64_t idx; int ret = index(name, idx); return ret ? ret : get_timestamp(idx, tz, value);
  }
  int inner_get_number(int64_t idx, common::number::ObNumber &value, IAllocator &allocator) const override {
    common::number::ObNumber number; int ret = get_number(idx, number); return ret ? ret : value.from(number, allocator);
  }
  int inner_get_number(const char *name, common::number::ObNumber &value, IAllocator &allocator) const override {
    int64_t idx; int ret = index(name, idx); return ret ? ret : inner_get_number(idx, value, allocator);
  }
};
int inner_call(uint64_t namespace_id, SessionBinding *&binding,
               ObInnerSQLConnection &connection, Frame payload, int64_t &affected) {
  if (payload.ret) { return payload.ret; }
  if (namespace_id == 0 || namespace_id >= (1ULL << 30)) { return OB_INVALID_ARGUMENT; }
  if (binding && binding->channel->storage_space
          != StorageSpaceHandle::namespace_space(namespace_id)) {
    close_session(binding);
    binding = nullptr;
  }
  int ret = binding ? OB_SUCCESS : open_session(
      namespace_id, connection.get_session(), binding, true);
  if (ret) { return ret; }
  int64_t deadline = 0;
  Frame request = inner_request(*binding, connection, std::move(payload), deadline);
  if (request.ret) { return request.ret; }
  InnerExchange stream(*binding, std::move(request), deadline);
  Frame frame;
  while (!(ret = stream.next(frame))) {
    if (frame.type() != 'o') { return OB_INVALID_ARGUMENT; }
    affected = frame.number();
    if (!frame.consumed()) { return OB_INVALID_ARGUMENT; }
  }
  return ret == OB_ITER_END ? OB_SUCCESS : ret;
}
int inner_read(uint64_t namespace_id, SessionBinding *&binding,
               ObInnerSQLConnection &connection, const ObString &sql,
               common::ObISQLClient::ReadResult &result, bool is_user_sql) {
  result.reuse();
  if (namespace_id == 0 || namespace_id >= (1ULL << 30)) { return OB_INVALID_ARGUMENT; }
  if (binding && binding->channel->storage_space
          != StorageSpaceHandle::namespace_space(namespace_id)) {
    close_session(binding);
    binding = nullptr;
  }
  int ret = binding ? OB_SUCCESS : open_session(
      namespace_id, connection.get_session(), binding, true);
  if (ret) { return ret; }
  Frame payload('?', MAX_SQL_MESSAGE); payload.number('R'); payload.number(is_user_sql); payload.string(sql);
  InnerResult *handler = nullptr;
  int64_t deadline = 0;
  Frame request = inner_request(*binding, connection, std::move(payload), deadline);
  if (request.ret) { return request.ret; }
  ret = result.create_handler(handler, connection, *binding, request, deadline);
  if (!ret) { ret = handler->init(); }
  return ret;
}
} } }
