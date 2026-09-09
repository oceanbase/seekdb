/* Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "graph.h"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#ifdef GRAPH_P0_MYSQL
#include <mysql.h>
#endif
using namespace graph_p0;
static volatile std::sig_atomic_t cancelled = 0;
static void cancel(int) { cancelled = 1; }
[[maybe_unused]] static void check_cancel() { if (cancelled) throw std::runtime_error("cancelled"); }
[[maybe_unused]] static size_t number(const char *s) {
  std::string v(s); size_t end = 0;
  if (v.empty() || v[0] == '-') throw std::invalid_argument("nonnegative integer required");
  auto n = std::stoull(v, &end);
  if (end != v.size()) throw std::invalid_argument("invalid integer");
  return n;
}
#ifdef GRAPH_P0_MYSQL
static const char *env(const char *key, const char *fallback) {
  const char *s = std::getenv(key); return s ? s : fallback;
}
// Real-server adapter for the P0 controller. SQL commands reach the existing
// DAS path. This is NOT an in-process DAS iterator or statement snapshot API.
class SqlAccess final : public IAccess {
public:
  SqlAccess() {
    mysql_ = mysql_init(nullptr);
    if (!mysql_) throw std::runtime_error("mysql_init failed");
    unsigned int timeout = 10;
    mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(mysql_, MYSQL_OPT_READ_TIMEOUT, &timeout);
    try {
      std::string db = env("GRAPH_P0_DATABASE", "graph_p0");
      if (db.compare(0, 8, "graph_p0") != 0) throw std::invalid_argument("database must start with graph_p0");
      if (!mysql_real_connect(mysql_, env("GRAPH_P0_HOST", "127.0.0.1"),
                              env("GRAPH_P0_USER", "root"), env("GRAPH_P0_PASSWORD", ""),
                              db.c_str(), number(env("GRAPH_P0_PORT", "2881")), nullptr, 0))
        throw std::runtime_error(mysql_error(mysql_));
      query("SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ");
      query("SET SESSION ob_query_timeout=5000000");
      query("SET SESSION ob_trx_timeout=60000000");
      query("START TRANSACTION WITH CONSISTENT SNAPSHOT");
    } catch (...) { mysql_close(mysql_); mysql_ = nullptr; throw; }
  }
  ~SqlAccess() override {
    if (mysql_) { mysql_query(mysql_, "ROLLBACK"); mysql_close(mysql_); }
  }
  using Result = std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>;
  Result query(const std::string &sql) {
    check_cancel();
    ++statements;
    if (mysql_real_query(mysql_, sql.data(), sql.size())) throw std::runtime_error(mysql_error(mysql_));
    Result result(mysql_store_result(mysql_), mysql_free_result);
    if (!result && mysql_field_count(mysql_)) throw std::runtime_error(mysql_error(mysql_));
    return result;
  }
  std::vector<Binding> bindings() {
    auto r = query("SELECT binding_id,tenant,id FROM gp_binding ORDER BY binding_id LIMIT 4097");
    std::vector<Binding> out;
    while (auto row = mysql_fetch_row(r.get())) out.push_back({integer(row[0]), {integer(row[1]), integer(row[2])}});
    if (out.size() > 4096) throw std::runtime_error("input binding limit exceeded");
    return out;
  }
  std::set<Key> vertices(const std::set<Key> &keys) override {
    if (keys.empty()) return {};
    auto r = query("SELECT tenant,id FROM gp_vertex WHERE (tenant,id) IN (" + tuples(keys) + ")");
    std::set<Key> out;
    while (auto row = mysql_fetch_row(r.get())) out.insert({integer(row[0]), integer(row[1])});
    return out;
  }
  std::vector<Edge> edges(const std::set<Key> &keys, Direction d, const Key *after, size_t limit) override {
    if (keys.empty()) return {};
    std::string prefix = d == Direction::Out ? "src" : "dst";
    std::string sql = "SELECT tenant,id,src_tenant,src,dst_tenant,dst FROM gp_edge WHERE (" +
                      prefix + "_tenant," + prefix + ") IN (" + tuples(keys) + ")";
    sql += " AND src_tenant IS NOT NULL AND src IS NOT NULL AND dst_tenant IS NOT NULL AND dst IS NOT NULL";
    if (after) sql += " AND (tenant,id)>(" + tuple(*after) + ")";
    sql += " ORDER BY tenant,id LIMIT " + std::to_string(limit);
    auto r = query(sql);
    std::vector<Edge> out;
    while (auto row = mysql_fetch_row(r.get()))
      out.push_back({{integer(row[0]), integer(row[1])}, {integer(row[2]), integer(row[3])},
                     {integer(row[4]), integer(row[5])}});
    return out;
  }
  size_t statements = 0;
private:
  static int64_t integer(const char *s) {
    if (!s) throw std::runtime_error("unexpected NULL key");
    std::string v(s); size_t end = 0; auto n = std::stoll(v, &end);
    if (end != v.size()) throw std::runtime_error("invalid key");
    return n;
  }
  static std::string tuple(const Key &k) { return std::to_string(k.tenant) + "," + std::to_string(k.id); }
  static std::string tuples(const std::set<Key> &keys) {
    std::vector<std::string> out;
    for (const auto &k : keys) out.push_back("(" + tuple(k) + ")");
    return join(out, ",");
  }
  MYSQL *mysql_ = nullptr;
};
#endif
int main(int argc, char **argv) {
  try {
    std::signal(SIGINT, cancel); std::signal(SIGTERM, cancel);
    if (argc == 3 && std::string(argv[1]) == "sql") {
      const std::string name(argv[2]);
      std::cout << (name == "batch_out" ? lower_batch(Direction::Out) :
                    name == "batch_in" ? lower_batch(Direction::In) : lower(example(name))) << ";\n";
    } else if (argc == 5 && std::string(argv[1]) == "expand") {
#ifdef GRAPH_P0_MYSQL
      std::string direction(argv[2]);
      if (direction != "out" && direction != "in") throw std::invalid_argument("direction must be out/in");
      SqlAccess access;
      SingleHop hop(access, number(argv[3]), number(argv[4]), check_cancel);
      hop.open(access.bindings(), direction == "out" ? Direction::Out : Direction::In);
      Row row;
      while (hop.next(row)) std::cout << row.binding << '\t' << row.edge.tenant << '\t' << row.edge.id
                                    << '\t' << row.target.tenant << '\t' << row.target.id << '\n';
      const auto &m = hop.metrics();
      std::cerr << "{\"input_bindings\":" << m.input_bindings << ",\"source_keys\":" << m.source_keys
                << ",\"edge_rows_returned\":" << m.edge_rows << ",\"target_keys_requested\":" << m.target_keys
                << ",\"output_rows\":" << m.output_rows << ",\"controller_budget_charge\":" << m.peak_buffer_bytes
                << ",\"sql_statements\":" << access.statements << "}\n";
#else
      throw std::runtime_error("built without MySQL adapter");
#endif
    } else {
      throw std::invalid_argument("usage: graph_p0 sql out|in|chain|trail|converge|filtered|works; graph_p0 expand out|in PAGE_SIZE BUDGET_BYTES");
    }
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "graph_p0: " << e.what() << '\n';
    // stdout can already contain rows: callers MUST require exit code zero.
    return 1;
  }
}
