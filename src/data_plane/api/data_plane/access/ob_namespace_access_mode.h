#ifndef OCEANBASE_DATA_PLANE_ACCESS_OB_NAMESPACE_ACCESS_MODE_H_
#define OCEANBASE_DATA_PLANE_ACCESS_OB_NAMESPACE_ACCESS_MODE_H_

#include <cstdint>

namespace oceanbase
{
namespace data_plane
{

// Bound by the namespace storage session before an encoded tablet reaches
// storage. Native tablets do not need a namespace access policy.
enum class ObNamespaceAccessMode : uint8_t
{
  UNBOUND,
  UNFENCED,
  LEASED
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_ACCESS_OB_NAMESPACE_ACCESS_MODE_H_
