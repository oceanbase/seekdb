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

#include <cstdint>

namespace oceanbase
{
namespace ns
{

class Namespace;
class NamespaceRuntime;
class NamespaceRegistry;

} // namespace ns
} // namespace oceanbase

#endif // OCEANBASE_NAMESPACE_NAMESPACE_H_
