#include "namespace/namespace.h"
#include "namespace/catalog.h"

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

bool Namespace::bind_name_if_empty(const char *name)
{
  if (name == nullptr || name[0] == '\0' || name_[0] != '\0'
      || std::strlen(name) >= MAX_NAME_LEN) { return false; }
  std::strcpy(name_, name);
  return true;
}

struct NamespaceRegistry::Impl
{
  struct Entry
  {
    Entry(uint64_t id, const char *name) : ns(id, name), runtime(ns) {}
    Namespace ns;
    NamespaceRuntime runtime;
    uint64_t connections = 0;
    bool closing = false;
    bool registered = true;
  };
  std::mutex mutex;
  std::unordered_map<uint64_t, Entry *> entries;
  NamespaceControlState control_state;
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
  if (impl_ == nullptr || id == 0 || id >= NamespaceObjectKey::NAMESPACE_LIMIT) { return -1; }
  Impl::Entry *entry = new (std::nothrow) Impl::Entry(id, name);
  if (entry == nullptr) { return -2; }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const auto existing = impl_->entries.find(id);
  if (existing != impl_->entries.end()) {
    if (!existing->second->registered) { delete entry; return -1; }
    const bool same_name = name != nullptr
        && std::strcmp(existing->second->ns.name(), name) == 0;
    const bool bound = existing->second->ns.bind_name_if_empty(name);
    delete entry;
    return same_name || bound ? 0 : -1;
  }
  impl_->entries.emplace(id, entry);
  return 0;
}

bool NamespaceRegistry::get(uint64_t id, NamespaceRuntime *&runtime)
{
  runtime = nullptr;
  if (impl_ == nullptr) { return false; }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const auto it = impl_->entries.find(id);
  if (it == impl_->entries.end() || !it->second->registered) { return false; }
  runtime = &it->second->runtime;
  return true;
}

bool NamespaceRegistry::find(const char *name, NamespaceRuntime *&runtime)
{
  runtime = nullptr;
  if (impl_ == nullptr || name == nullptr || name[0] == '\0') { return false; }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  for (auto &it : impl_->entries) {
    if (it.second->registered && !it.second->closing
        && it.second->ns.name()[0] != '\0'
        && std::strcmp(it.second->ns.name(), name) == 0) {
      runtime = &it.second->runtime;
      return true;
    }
  }
  return false;
}

void NamespaceRegistry::list_ids(std::vector<uint64_t> &ids)
{
  ids.clear();
  if (impl_ != nullptr) {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    ids.reserve(impl_->entries.size());
    for (const auto &entry : impl_->entries) {
      if (entry.second->registered && !entry.second->closing) { ids.push_back(entry.first); }
    }
  }
}

bool NamespaceRegistry::acquire_session(uint64_t id)
{
  if (impl_ == nullptr) { return false; }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const auto it = impl_->entries.find(id);
  if (it == impl_->entries.end() || !it->second->registered
      || it->second->closing) { return false; }
  ++it->second->connections;
  return true;
}

void NamespaceRegistry::release_session(uint64_t id)
{
  if (impl_ == nullptr) { return; }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const auto it = impl_->entries.find(id);
  if (it != impl_->entries.end() && it->second->connections != 0) {
    --it->second->connections;
  }
}

bool NamespaceRegistry::begin_drop(uint64_t id)
{
  if (impl_ == nullptr) { return false; }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const auto it = impl_->entries.find(id);
  if (it == impl_->entries.end()) { return true; } // DELETING after restart.
  if (it->second->connections != 0) { return false; }
  it->second->closing = true;
  return true;
}

void NamespaceRegistry::cancel_drop(uint64_t id)
{
  if (impl_ == nullptr) { return; }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const auto it = impl_->entries.find(id);
  if (it != impl_->entries.end() && it->second->registered) {
    it->second->closing = false;
  }
}

void NamespaceRegistry::remove(uint64_t id)
{
  if (impl_ == nullptr) { return; }
  std::lock_guard<std::mutex> guard(impl_->mutex);
  const auto it = impl_->entries.find(id);
  // Existing background owners may still hold Runtime pointers.
  if (it != impl_->entries.end()) { it->second->registered = false; }
}

NamespaceControlState &NamespaceRegistry::control_state()
{
  return impl_->control_state;
}

NamespaceRegistry &namespace_registry()
{
  // The single sanctioned new global; every other service stays ns-blind.
  static NamespaceRegistry registry;
  return registry;
}

} // namespace ns
} // namespace oceanbase
