#ifndef OCEANBASE_NAMESPACE_CATALOG_H_
#define OCEANBASE_NAMESPACE_CATALOG_H_

// Persistent namespace catalog values and their storage-independent encoding.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace oceanbase {
namespace ns {

struct CatalogPageRef { uint64_t page = 0; int64_t cap = 0; };
struct CatalogValue { std::string data; int64_t cap = 0; };
struct CatalogNode {
  bool leaf = true;
  std::vector<std::string> keys;
  std::vector<CatalogValue> values;
  std::vector<CatalogPageRef> children;
};
struct CatalogRoots {
  uint64_t source = 0;
  CatalogPageRef catalog, directory;
  int64_t snapshot = 0, schema_version = 0;
  uint64_t snapshot_ref = 0;
  uint64_t parent_ref = 0; int64_t ref_count = 0; // Canonical snapshot rows only.
  int64_t state = 0; // 0 LIVE, 1 DELETING, 2 DELETED; ids are never reused.
  int64_t active_schema_changes = 0;
  int64_t pending_schema_version = 0;
};

class NamespaceCatalogCodec final {
public:
  static constexpr size_t FANOUT = 8;
  static int64_t cap_min(int64_t a, int64_t b);
  static std::string object_key(uint64_t id);
  static std::string encode_entry(uint64_t schema_object, uint64_t local_table,
                                  uint64_t local_tablet, uint64_t bound_tablet = 0);
  static bool decode_entry(const std::string &data, uint64_t &schema_object,
                           uint64_t &local_table, uint64_t &local_tablet,
                           uint64_t &bound_tablet);
  static std::string encode_node(const CatalogNode &node);
  static bool decode_node(const std::string &data, int64_t cap, CatalogNode &node);
};

} // namespace ns
} // namespace oceanbase

#endif // OCEANBASE_NAMESPACE_CATALOG_H_
