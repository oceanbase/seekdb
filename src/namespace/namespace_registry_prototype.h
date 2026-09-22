/**
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OCEANBASE_NAMESPACE_NAMESPACE_REGISTRY_PROTOTYPE_H_
#define OCEANBASE_NAMESPACE_NAMESPACE_REGISTRY_PROTOTYPE_H_

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"

// Single-process namespace boundary layer (issue 03, Phase 1a).
//
// This header holds the three domain objects of the single-process pivot:
// Namespace (identity/lineage/storage root, pure metadata), NamespaceRuntime
// (the per-namespace service group holder) and NamespaceRegistry (the ns name/id
// map, the one new global). Per docs/adr/0003-no-ambient-context.md the registry
// is an explicitly owned object: it is reached through the owner that holds it,
// never through a thread_local and never through a new get_instance().
//
// Header-only on purpose: the CMake and bazel builds compile an explicit source
// inventory, so a header keeps this boundary layer buildable until Phase 1
// creates its translation unit together with the Runtime service group.
//
// Namespace identity is interpreted only at this boundary. The storage engine
// stays ns-blind and the SQL layer never receives a ns_id (invariant 2b).

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
} // namespace schema
} // namespace share

namespace namespace_fork
{

// Namespace 1 is the bootstrap namespace: the system namespace that also serves
// as the default user namespace. Its id is fixed by the existing tablet id
// encoding (encode(1, local) == local), so it is a property of the domain model,
// not a tunable.
static const uint64_t SYSTEM_NAMESPACE_ID = 1;
// Reserved per the identity decision: user namespace names may not use the "__"
// prefix.
static const char *const NAMESPACE_RESERVED_NAME_PREFIX = "__";

// Namespace: identity, lineage and storage root. Pure metadata; it holds no
// runtime state at all.
class Namespace
{
public:
  Namespace()
    : id_(OB_INVALID_ID), name_(), parent_id_(OB_INVALID_ID), fork_scn_(0), valid_(false)
  {}
  Namespace(const uint64_t id,
            const common::ObString &name,
            const uint64_t parent_id,
            const int64_t fork_scn)
    : id_(id), name_(), parent_id_(parent_id), fork_scn_(fork_scn), valid_(true)
  {
    name_.assign_ptr(name.ptr(), name.length());
  }

  bool is_valid() const { return valid_ && OB_INVALID_ID != id_ && SYSTEM_NAMESPACE_ID <= id_; }
  uint64_t get_id() const { return id_; }
  const common::ObString &get_name() const { return name_; }
  uint64_t get_parent_id() const { return parent_id_; }
  int64_t get_fork_scn() const { return fork_scn_; }
  // The bootstrap namespace is its own lineage root.
  bool is_system_namespace() const { return SYSTEM_NAMESPACE_ID == id_; }

  // A namespace name is usable unless it is empty or uses the reserved "__"
  // prefix. The length bound is enforced by the caller against the account name
  // limit it already applies.
  static bool is_valid_name(const common::ObString &name)
  {
    return !name.empty() && !name.prefix_match(NAMESPACE_RESERVED_NAME_PREFIX);
  }

  TO_STRING_KV(K_(id), K_(name), K_(parent_id), K_(fork_scn), K_(valid));

private:
  uint64_t id_;
  common::ObString name_;
  uint64_t parent_id_;
  int64_t fork_scn_;
  bool valid_;
};

// NamespaceRuntime: the in-process runtime of one Namespace, i.e. the holder of
// that namespace's service group (schema service, plan cache, session manager,
// schedulers, ...). It is a pure compute layer: it is constructed once, bound to
// one namespace, and interprets no namespace identity internally.
//
// Issue 03 creates the skeleton with an explicitly injected schema service.
// Issue 04 adds the remaining services of the group; issue 05 makes the hot path
// use them through the session binding.
class NamespaceRuntime
{
public:
  NamespaceRuntime()
    : namespace_(), schema_service_(nullptr), active_(false)
  {}

  // The runtime owns its copy of the namespace metadata, so the caller is free
  // to pass a temporary and no lifetime coupling exists between the two.
  int init(const Namespace &ns)
  {
    int ret = OB_SUCCESS;
    if (!ns.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (active_) {
      ret = OB_INIT_TWICE;
    } else {
      namespace_ = ns;
      active_ = true;
    }
    return ret;
  }

  bool is_active() const { return active_; }
  const Namespace *get_namespace() const { return active_ ? &namespace_ : nullptr; }
  uint64_t get_namespace_id() const
  {
    return active_ ? namespace_.get_id() : OB_INVALID_ID;
  }

  // The per-namespace schema service. For now it is the injected server schema
  // service; issue 04 replaces it with the instance this runtime owns.
  void set_schema_service(share::schema::ObMultiVersionSchemaService *schema_service)
  {
    schema_service_ = schema_service;
  }
  share::schema::ObMultiVersionSchemaService *get_schema_service() const
  {
    return schema_service_;
  }

private:
  Namespace namespace_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  bool active_;
};

// NamespaceRegistry: the process-wide map from namespace name/id to Namespace and
// its NamespaceRuntime. This is the only new global of the single-process pivot,
// and it is owned explicitly (currently by ObServer) rather than reachable via
// get_instance(). It is one of the three legitimate interpreters of namespace
// identity, next to login routing and the tablet id translation layer.
class NamespaceRegistry
{
public:
  NamespaceRegistry() : inited_(false) {}
  ~NamespaceRegistry() {}

  int init()
  {
    int ret = OB_SUCCESS;
    if (inited_) {
      ret = OB_INIT_TWICE;
    } else {
      inited_ = true;
    }
    return ret;
  }

  // Register a namespace and its runtime. Names and ids are both unique; the
  // caller keeps ownership of the runtime object, which must outlive the
  // registry. An empty name registers an id-reachable namespace (one whose
  // authoritative name lives in the system namespace's control metadata, so
  // this process only routes by id).
  int register_namespace(const uint64_t id,
                         const common::ObString &name,
                         const uint64_t parent_id,
                         NamespaceRuntime &runtime)
  {
    int ret = OB_SUCCESS;
    const Namespace ns(id, name, parent_id, 0 /* fork scn */);
    if (!inited_) {
      ret = OB_NOT_INIT;
    } else if (!ns.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (runtimes_.end() != runtimes_.find(id)) {
      ret = OB_ENTRY_EXIST;
    } else if (!name.empty() && !Namespace::is_valid_name(name)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(runtime.init(ns))) {
      // The runtime rejects a second binding, so retry safety comes for free.
    } else {
      runtimes_.insert(std::make_pair(id, &runtime));
      namespaces_.insert(std::make_pair(id, ns));
      if (!name.empty()) {
        std::string key(name.ptr(), name.length());
        names_.insert(std::make_pair(key, id));
      }
    }
    return ret;
  }

  // Name-keyed registration, used for namespaces this process can route by name
  // (the system namespace, and any namespace created in this process).
  int register_namespace(const Namespace &ns, NamespaceRuntime &runtime)
  {
    return register_namespace(ns.get_id(), ns.get_name(), ns.get_parent_id(), runtime);
  }

  int get_runtime(const uint64_t ns_id, NamespaceRuntime *&runtime) const
  {
    int ret = OB_SUCCESS;
    runtime = nullptr;
    std::map<uint64_t, NamespaceRuntime *>::const_iterator it = runtimes_.find(ns_id);
    if (runtimes_.end() == it) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      runtime = it->second;
    }
    return ret;
  }

  int get_namespace(const uint64_t ns_id, const Namespace *&ns) const
  {
    int ret = OB_SUCCESS;
    ns = nullptr;
    std::map<uint64_t, Namespace>::const_iterator it = namespaces_.find(ns_id);
    if (namespaces_.end() == it) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      ns = &it->second;
    }
    return ret;
  }

  // Resolve a namespace name. Login routing and management statements are the
  // only callers.
  int resolve_name(const common::ObString &name, uint64_t &ns_id) const
  {
    int ret = OB_SUCCESS;
    ns_id = OB_INVALID_ID;
    std::string key(name.ptr(), name.length());
    std::map<std::string, uint64_t>::const_iterator it = names_.find(key);
    if (names_.end() == it) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      ns_id = it->second;
    }
    return ret;
  }

  int64_t count() const { return static_cast<int64_t>(runtimes_.size()); }
  bool is_inited() const { return inited_; }

private:
  bool inited_;
  std::map<uint64_t, Namespace> namespaces_;
  std::map<std::string, uint64_t> names_;
  std::map<uint64_t, NamespaceRuntime *> runtimes_;
};

// Login routing: split the "root@ns" spelling that the MySQL handshake carries
// as the user name. An absent "@" means the default namespace, i.e. the system
// namespace, so existing clients and tooling keep working unchanged.
//
// The argument is the raw handshake user name. On return, user_name is the
// account name with any "@ns" suffix removed and ns_name is the requested
// namespace (empty when none was given).
inline int split_login_namespace(const common::ObString &in,
                                 common::ObString &user_name,
                                 common::ObString &ns_name)
{
  int ret = OB_SUCCESS;
  user_name = in;
  ns_name.reset();
  const char *const at = static_cast<const char *>(memchr(in.ptr(), '@', in.length()));
  if (nullptr != at) {
    const int32_t offset = static_cast<int32_t>(at - in.ptr());
    if (0 == offset || offset == in.length() - 1) {
      // "" or "user@" or "@ns": not a namespace-qualified account name.
      ret = OB_INVALID_ARGUMENT;
    } else {
      user_name.assign_ptr(in.ptr(), offset);
      ns_name.assign_ptr(in.ptr() + offset + 1, in.length() - offset - 1);
    }
  }
  return ret;
}

} // namespace namespace_fork
} // namespace oceanbase

#endif // OCEANBASE_NAMESPACE_NAMESPACE_REGISTRY_PROTOTYPE_H_
