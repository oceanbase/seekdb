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
#include <iostream>
using namespace graph_p0;
static void require(bool b) { if (!b) throw std::runtime_error("test failed"); }
template<class F> void rejects(F f) {
  bool failed = false;
  try { f(); } catch (const std::exception &) { failed = true; }
  require(failed);
}
class FaultAccess final : public IAccess {
public:
  std::set<Key> keys = {{1,1}, {1,2}, {1,3}, {2,1}};
  std::vector<Edge> data = {{{1,1},{1,1},{1,3}}, {{1,2},{1,1},{1,3}},
                           {{1,3},{1,2},{1,3}}, {{1,4},{1,3},{1,3}},
                           {{1,5},{1,1},{9,9}}, {{1,6},{9,9},{1,1}},
                           {{2,1},{2,1},{1,3}}};
  int calls = 0, fail_at = -1;
  void touch() { if (++calls == fail_at) throw std::runtime_error("injected read failure"); }
  std::set<Key> vertices(const std::set<Key> &requested) override {
    touch(); std::set<Key> out;
    for (const auto &k : requested) if (keys.count(k)) out.insert(k);
    return out;
  }
  std::vector<Edge> edges(const std::set<Key> &sources, Direction d, const Key *after, size_t n) override {
    touch(); std::vector<Edge> out;
    for (const auto &e : data) {
      if (sources.count(d == Direction::Out ? e.src : e.dst) && (!after || *after < e.key)) out.push_back(e);
      if (out.size() == n) break;
    }
    return out;
  }
};
using Tuple = std::tuple<int64_t,int64_t,int64_t,int64_t,int64_t>;
static std::multiset<Tuple> drain(SingleHop &hop) {
  Row r; std::multiset<Tuple> out;
  while (hop.next(r)) out.emplace(r.binding,r.edge.tenant,r.edge.id,r.target.tenant,r.target.id);
  return out;
}
int main() {
  try {
    FaultAccess access;
    std::vector<Binding> bindings = {{10,{1,1}}, {11,{1,1}}, {12,{1,2}}, {13,{2,1}}, {14,{9,9}}};
    const std::multiset<Tuple> expected = {{10,1,1,1,3}, {10,1,2,1,3}, {11,1,1,1,3},
                                          {11,1,2,1,3}, {12,1,3,1,3}, {13,2,1,1,3}};
    for (size_t page : {1,2,64}) {
      SingleHop hop(access,page,1<<20);
      hop.open(bindings,Direction::Out); require(drain(hop) == expected);
      require(hop.metrics().output_rows == 6 && hop.metrics().edge_rows == 5);
      hop.open(bindings,Direction::Out); require(drain(hop) == expected); // rescan
      hop.open({},Direction::Out); require(drain(hop).empty());
      hop.open({{0,{1,3}}},Direction::In); require(drain(hop).size() == 5);
    }
    SingleHop tiny(access,2,1); rejects([&] { tiny.open(bindings,Direction::Out); });
    SingleHop normal(access,2,1<<20);
    rejects([&] { normal.open({{1,{1,1}},{1,{1,2}}},Direction::Out); });
    for (int fault : {1,2,3,4,5}) {
      access.calls = 0; access.fail_at = fault;
      rejects([&] { normal.open(bindings,Direction::Out); drain(normal); });
      access.fail_at = -1;
      normal.open(bindings,Direction::Out); require(drain(normal) == expected);
    }
    int checks = 0;
    SingleHop cancelled_hop(access,1,1<<20,[&] { if (++checks == 6) throw std::runtime_error("cancel"); });
    rejects([&] { cancelled_hop.open(bindings,Direction::Out); drain(cancelled_hop); });
    cancelled_hop.reset();
    rejects([] { ident("x`;DROP TABLE x"); });
    rejects([] { lower(example("unknown")); });
    auto invalid = example("out"); invalid.edges[0].left = "missing";
    rejects([&] { lower(invalid); });
    require(lower(example("trail")).find("<>") != std::string::npos);
    std::set<Identity> identities = {{1,{1,1}},{1,{2,1}},{2,{1,1}}};
    require(identities.size() == 3);
    std::cout << "PASS: association, multiplicity, composite identity, direction, paging, rescan, empty, cancellation, read failure, budget, lowering validation\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
