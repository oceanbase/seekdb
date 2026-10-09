#include "namespace/namespace.h"
#include <cassert>
#include <iostream>
using namespace oceanbase::ns;
int main() {
  NamespaceRegistry registry;
  assert(registry.add(2, "template", false) == 0);
  assert(registry.add(3, "user", true) == 0);
  NamespaceRuntime *runtime = nullptr;
  assert(registry.find("template", runtime));
  assert(runtime != nullptr && !runtime->ns().allows_login());
  for (int i = 0; i < NamespaceRuntime::SLOT_COUNT; ++i) {
    assert(runtime->service(static_cast<NamespaceRuntime::ServiceSlot>(i)) == nullptr);
  }
  assert(!registry.acquire_session(2));
  assert(registry.acquire_session(3));
  registry.release_session(3);
  assert(registry.add(2, "template", true) == -1);
  assert(!registry.acquire_session(2));
  assert(registry.add(2, "template", false) == 0);
  registry.remove(2);
  assert(!registry.find("template", runtime));
  std::cout << "PASS explicit login admission, internal identity and no service activation\n";
}
