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
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace graph_p0 {
// Typed composite identity. Mapping identity is separate from label and key.
struct Key {
  int64_t tenant = 0;
  int64_t id = 0;
  bool operator<(const Key &o) const { return std::tie(tenant, id) < std::tie(o.tenant, o.id); }
  bool operator==(const Key &o) const { return tenant == o.tenant && id == o.id; }
};
struct Identity {
  int mapping = 0;
  Key key;
  bool operator<(const Identity &o) const {
    return mapping == o.mapping ? key < o.key : mapping < o.mapping;
  }
};
struct Binding { int64_t id; Key key; };
struct Edge { Key key; Key src; Key dst; };
struct Row { int64_t binding; Key edge; Key target; };
enum class Direction { Out, In };
struct Metrics {
  size_t input_bindings = 0, source_keys = 0, edge_rows = 0;
  size_t target_keys = 0, output_rows = 0, peak_buffer_bytes = 0;
};

// Backend must keep one read view for the whole invocation, never return a
// successful empty page on a read error, and return strictly key-ordered pages.
class IAccess {
public:
  virtual ~IAccess() = default;
  virtual std::set<Key> vertices(const std::set<Key> &keys) = 0;
  virtual std::vector<Edge> edges(const std::set<Key> &sources, Direction direction,
                                  const Key *after, size_t limit) = 0;
};

// Test-only controller. Bounded edge pages; restoration streams instead of
// materializing page_size * binding_count rows. No cross-invocation cache.
class SingleHop {
public:
  SingleHop(IAccess &access, size_t page_size, size_t budget,
            std::function<void()> check = [] {})
      : access_(access), page_size_(page_size), budget_(budget), check_(std::move(check)) {
    if (!page_size_ || page_size_ > 4096) throw std::invalid_argument("page size must be 1..4096");
  }
  void open(const std::vector<Binding> &bindings, Direction direction) {
    reset();
    if (bindings.size() > 4096) throw std::invalid_argument("at most 4096 bindings per invocation");
    // Conservative accounting of controller containers, NOT server/driver memory.
    account(bindings.size() * (sizeof(Binding) + 128) + page_size_ * (sizeof(Edge) + 256));
    check_();
    std::set<int64_t> ids;
    std::set<Key> keys;
    for (const auto &b : bindings) {
      if (!ids.insert(b.id).second) throw std::invalid_argument("binding ids must be unique");
      keys.insert(b.key);
    }
    bindings_ = bindings;
    direction_ = direction;
    metrics_.input_bindings = bindings.size();
    metrics_.source_keys = keys.size();
    sources_ = access_.vertices(keys); // Source existence is required too.
    opened_ = true;
  }
  bool next(Row &row) {
    if (!opened_) throw std::logic_error("open required");
    try {
      check_();
      while (true) {
        if (edge_index_ == page_.size()) {
          if (done_ || sources_.empty()) return false;
          page_ = access_.edges(sources_, direction_, has_after_ ? &after_ : nullptr, page_size_);
          if (page_.size() > page_size_) throw std::runtime_error("backend exceeded page bound");
          if (page_.empty()) { done_ = true; return false; }
          std::set<Key> targets;
          for (const auto &e : page_) {
            if (has_after_ && !(after_ < e.key)) throw std::runtime_error("nonmonotonic edge page");
            if (!sources_.count(source(e))) throw std::runtime_error("unrequested source");
            after_ = e.key;
            has_after_ = true;
            targets.insert(target(e));
          }
          metrics_.edge_rows += page_.size();
          metrics_.target_keys += targets.size();
          targets_ = access_.vertices(targets);
          edge_index_ = binding_index_ = 0;
        }
        const auto &edge = page_[edge_index_];
        if (targets_.count(target(edge))) {
          while (binding_index_ < bindings_.size()) {
            const auto &b = bindings_[binding_index_++];
            check_();
            if (b.key == source(edge)) {
              row = {b.id, edge.key, target(edge)};
              ++metrics_.output_rows;
              return true;
            }
          }
        }
        ++edge_index_;
        binding_index_ = 0;
      }
    } catch (...) { reset_state(); throw; }
  }
  void reset() { reset_state(); metrics_ = {}; }
  const Metrics &metrics() const { return metrics_; }
private:
  Key source(const Edge &e) const { return direction_ == Direction::Out ? e.src : e.dst; }
  Key target(const Edge &e) const { return direction_ == Direction::Out ? e.dst : e.src; }
  void account(size_t bytes) {
    if (bytes > budget_) throw std::runtime_error("controller memory budget exceeded");
    metrics_.peak_buffer_bytes = bytes;
  }
  void reset_state() {
    std::vector<Binding>().swap(bindings_);
    std::vector<Edge>().swap(page_);
    sources_.clear(); targets_.clear();
    edge_index_ = binding_index_ = 0;
    opened_ = done_ = has_after_ = false;
  }
  IAccess &access_;
  size_t page_size_, budget_;
  std::function<void()> check_;
  Direction direction_ = Direction::Out;
  std::vector<Binding> bindings_;
  std::set<Key> sources_, targets_;
  std::vector<Edge> page_;
  size_t edge_index_ = 0, binding_index_ = 0;
  bool opened_ = false, done_ = false, has_after_ = false;
  Key after_;
  Metrics metrics_;
};

// Already-bound pattern IR: table/column names are validated, not raw SQL.
inline std::string ident(const std::string &s) {
  if (s.empty() || s.size() > 64 || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_'))
    throw std::invalid_argument("invalid identifier");
  for (unsigned char c : s) if (!std::isalnum(c) && c != '_') throw std::invalid_argument("invalid identifier");
  return "`" + s + "`";
}
struct VertexPattern { std::string table, variable; };
struct EdgePattern { std::string table, variable, left, right; Direction direction; };
struct Column { std::string variable, column, alias; };
struct IntFilter { std::string variable, column; int64_t value; };
struct Pattern {
  std::vector<VertexPattern> vertices;
  std::vector<EdgePattern> edges;
  std::vector<Column> columns;
  std::vector<IntFilter> filters;
  bool different_edges = false;
};
inline std::string col(const std::string &v, const std::string &c) { return ident(v) + "." + ident(c); }
inline std::string join(const std::vector<std::string> &xs, const std::string &sep) {
  std::string r;
  for (const auto &x : xs) { if (!r.empty()) r += sep; r += x; }
  return r;
}
inline std::string lower(const Pattern &p) {
  if (p.vertices.empty() || p.edges.size() > 8 || p.columns.empty())
    throw std::invalid_argument("unsupported pattern size/projection");
  std::set<std::string> vars, vertex_vars;
  std::vector<std::string> from, where, output;
  for (const auto &v : p.vertices) {
    if (!vars.insert(v.variable).second) throw std::invalid_argument("duplicate variable");
    vertex_vars.insert(v.variable);
    from.push_back(ident(v.table) + " " + ident(v.variable));
  }
  for (const auto &e : p.edges) {
    if (!vars.insert(e.variable).second || !vertex_vars.count(e.left) || !vertex_vars.count(e.right))
      throw std::invalid_argument("invalid edge binding");
    from.push_back(ident(e.table) + " " + ident(e.variable));
    const auto &src = e.direction == Direction::Out ? e.left : e.right;
    const auto &dst = e.direction == Direction::Out ? e.right : e.left;
    where.push_back(col(src, "tenant") + "=" + col(e.variable, "src_tenant"));
    where.push_back(col(src, "id") + "=" + col(e.variable, "src"));
    where.push_back(col(dst, "tenant") + "=" + col(e.variable, "dst_tenant"));
    where.push_back(col(dst, "id") + "=" + col(e.variable, "dst"));
  }
  if (p.different_edges) {
    for (size_t i = 0; i < p.edges.size(); ++i) for (size_t j = i + 1; j < p.edges.size(); ++j) {
      const auto &a = p.edges[i], &b = p.edges[j];
      if (a.table == b.table)
        where.push_back("(" + col(a.variable, "tenant") + "<>" + col(b.variable, "tenant") +
                        " OR " + col(a.variable, "id") + "<>" + col(b.variable, "id") + ")");
    }
  }
  for (const auto &f : p.filters) {
    if (!vars.count(f.variable)) throw std::invalid_argument("unbound filter");
    where.push_back(col(f.variable, f.column) + "=" + std::to_string(f.value));
  }
  std::set<std::string> aliases;
  for (const auto &c : p.columns) {
    if (!vars.count(c.variable) || !aliases.insert(c.alias).second) throw std::invalid_argument("invalid projection");
    output.push_back(col(c.variable, c.column) + " AS " + ident(c.alias));
  }
  return "SELECT " + join(output, ",") + " FROM " + join(from, ",") +
         (where.empty() ? "" : " WHERE " + join(where, " AND "));
}
inline Pattern example(const std::string &name) {
  Pattern p;
  p.vertices = {{"gp_vertex", "a"}, {"gp_vertex", "b"}};
  p.edges = {{"gp_edge", "e", "a", "b", Direction::Out}};
  p.columns = {{"a", "tenant", "a_tenant"}, {"a", "id", "a_id"},
               {"e", "tenant", "e_tenant"}, {"e", "id", "e_id"},
               {"b", "tenant", "b_tenant"}, {"b", "id", "b_id"}};
  if (name == "in") p.edges[0].direction = Direction::In;
  else if (name == "filtered") p.filters = {{"e", "weight", 1}};
  else if (name == "works") { p.vertices[1].table = "gp_company"; p.edges[0].table = "gp_works"; }
  else if (name == "chain" || name == "trail" || name == "converge") {
    p.vertices.push_back({"gp_vertex", "c"});
    p.edges.push_back({"gp_edge", "f", name == "converge" ? "c" : "b",
                       name == "converge" ? "b" : "c", Direction::Out});
    p.columns.push_back({"f", "tenant", "f_tenant"});
    p.columns.push_back({"f", "id", "f_id"});
    p.columns.push_back({"c", "tenant", "c_tenant"});
    p.columns.push_back({"c", "id", "c_id"});
    p.different_edges = name == "trail";
  } else if (name != "out") throw std::invalid_argument("unknown example");
  return p;
}
// A single-statement access candidate: access-key deduplication happens before
// restoring binding multiplicity. The server owns the statement snapshot and
// lifecycle. CTEs describe sharing opportunities, not guaranteed materialization.
inline std::string lower_batch(Direction direction) {
  const std::string source = direction == Direction::Out ? "src" : "dst";
  const std::string target = direction == Direction::Out ? "dst" : "src";
  return "WITH source_keys AS (SELECT DISTINCT b.tenant,b.id FROM gp_binding b "
         "JOIN gp_vertex v ON v.tenant=b.tenant AND v.id=b.id), "
         "incident AS (SELECT e.tenant,e.id,e." + source + "_tenant AS st,e." + source +
         " AS si,e." + target + "_tenant AS dt,e." + target + " AS di FROM gp_edge e "
         "JOIN source_keys s ON s.tenant=e." + source + "_tenant AND s.id=e." + source + "), "
         "target_keys AS (SELECT DISTINCT dt,di FROM incident), "
         "targets AS (SELECT v.tenant,v.id FROM gp_vertex v JOIN target_keys k "
         "ON v.tenant=k.dt AND v.id=k.di) "
         "SELECT b.binding_id,e.tenant,e.id,t.tenant,t.id FROM gp_binding b "
         "JOIN incident e ON b.tenant=e.st AND b.id=e.si "
         "JOIN targets t ON t.tenant=e.dt AND t.id=e.di";
}
} // namespace graph_p0
