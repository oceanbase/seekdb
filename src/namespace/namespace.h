#ifndef OCEANBASE_NAMESPACE_NAMESPACE_H_
#define OCEANBASE_NAMESPACE_NAMESPACE_H_

// Boundary layer for namespace fork (see docs/research/namespace_single_process_plan.md).
//
// This package is the ONLY place allowed to interpret ns identity:
//   - Namespace: identity / lineage / storage root (pure metadata)
//   - NamespaceRuntime: per-ns service group holder (pure compute layer)
//   - NamespaceRegistry: the single global registry
//
// Dependency direction is one-way: namespace -> observer -> sql -> storage.
// Lower layers (sql/storage/share) must NOT depend on this package; that is
// enforced by the visibility whitelist in BUILD.bazel.
//
// This layer depends on nothing inside src/ (STL only) so it can sit at the
// bottom of the layering rules. Error codes are plain ints; callers map them
// to OB error codes.

#include <cstdint>

namespace oceanbase
{
namespace ns
{

// Identity, lineage and storage root of one namespace. Pure metadata.
class Namespace final
{
public:
  static constexpr int64_t MAX_NAME_LEN = 128;
  Namespace(uint64_t id, const char *name);
  uint64_t id() const { return id_; }
  const char *name() const { return name_; }
private:
  uint64_t id_;
  char name_[MAX_NAME_LEN];
};

// Per-namespace service group holder (pure compute layer). Phase 1a is a
// skeleton bound to sessions at login; later phases move the per-ns service
// instances (schema service, plan cache, session mgr, ...) in here.
class NamespaceRuntime final
{
public:
  // Typed service instances are owned by the observer/sql layers; the
  // boundary layer only carries opaque pointers so it stays STL-only.
  enum ServiceSlot : uint8_t
  {
    SCHEMA_SERVICE = 0,
    PLAN_CACHE = 1,
    ROOT_COMMAND_SERVICE = 2,
    DIRECT_INSERT_SERVICE = 3,
    DIRECT_INSERT_ROUTES = 4,
    SQL_PROXY = 5,
    TABLET_AUTOINCREMENT_SERVICE = 6,
    SLOT_COUNT
  };
  explicit NamespaceRuntime(Namespace &ns) : ns_(ns) {}
  Namespace &ns() const { return ns_; }
  void set_service(ServiceSlot slot, void *service)
  {
    if (slot < SLOT_COUNT) { services_[slot] = service; }
  }
  void *service(ServiceSlot slot) const
  {
    return slot < SLOT_COUNT ? services_[slot] : nullptr;
  }
private:
  Namespace &ns_;
  void *services_[SLOT_COUNT] = {};
};

// The single sanctioned new global (ADR 0003). Everything else stays
// ns-blind. Owns all Namespace/NamespaceRuntime instances.
class NamespaceRegistry final
{
public:
  NamespaceRegistry();
  ~NamespaceRegistry();
  // 0 on success, -1 duplicate/invalid argument, -2 allocation failure.
  int add(uint64_t id, const char *name);
  // Returns true when found. Entries with an empty name never match find().
  bool get(uint64_t id, NamespaceRuntime *&runtime);
  bool find(const char *name, NamespaceRuntime *&runtime);
private:
  struct Impl;
  Impl *impl_;
};

// The one allowed new process-wide global. Constructed on first use.
NamespaceRegistry &namespace_registry();

} // namespace ns
} // namespace oceanbase

#endif // OCEANBASE_NAMESPACE_NAMESPACE_H_
