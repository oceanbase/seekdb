#include "namespace/namespace.h"

#include <cstring>
#include <mutex>
#include <new>
#include <unordered_map>

namespace oceanbase
{
namespace ns
{

Namespace::Namespace(uint64_t id, const char *name) : id_(id), name_()
{
  if (name != nullptr) {
    std::strncpy(name_, name, MAX_NAME_LEN - 1);
    name_[MAX_NAME_LEN - 1] = '\0';
  }
}

struct NamespaceRegistry::Impl
{
  struct Entry
  {
    Entry(uint64_t id, const char *name) : ns(id, name), runtime(ns) {}
    Namespace ns;
    NamespaceRuntime runtime;
  };
  std::mutex mutex;
  std::unordered_map<uint64_t, Entry *> entries;
};

NamespaceRegistry::NamespaceRegistry() : impl_(new (std::nothrow) Impl()) {}

NamespaceRegistry::~NamespaceRegistry()
{
  if (impl_ != nullptr) {
    for (auto &it : impl_->entries) { delete it.second; }
    delete impl_;
  }
}

int NamespaceRegistry::add(uint64_t id, const char *name)
{
  if (impl_ == nullptr || id == 0 || id >= (1ULL << 30)) { return -1; }
  Impl::Entry *entry = new (std::nothrow) Impl::Entry(id, name);
  if (entry == nullptr) { return -2; }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  if (!impl_->entries.emplace(id, entry).second) {
    delete entry;
    return -1;
  }
  return 0;
}

bool NamespaceRegistry::get(uint64_t id, NamespaceRuntime *&runtime)
{
  runtime = nullptr;
  if (impl_ == nullptr) { return false; }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const auto it = impl_->entries.find(id);
  if (it == impl_->entries.end()) { return false; }
  runtime = &it->second->runtime;
  return true;
}

bool NamespaceRegistry::find(const char *name, NamespaceRuntime *&runtime)
{
  runtime = nullptr;
  if (impl_ == nullptr || name == nullptr || name[0] == '\0') { return false; }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  for (auto &it : impl_->entries) {
    if (it.second->ns.name()[0] != '\0' && std::strcmp(it.second->ns.name(), name) == 0) {
      runtime = &it.second->runtime;
      return true;
    }
  }
  return false;
}

NamespaceRegistry &namespace_registry()
{
  // The single sanctioned new global; every other service stays ns-blind.
  static NamespaceRegistry registry;
  return registry;
}

} // namespace ns
} // namespace oceanbase
