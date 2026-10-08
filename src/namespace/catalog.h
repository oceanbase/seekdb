#ifndef OCEANBASE_NAMESPACE_CATALOG_H_
#define OCEANBASE_NAMESPACE_CATALOG_H_

// Persistent namespace catalog values and their storage-independent encoding.
#include "storage/physical_snapshot_retention.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace oceanbase {
namespace ns {

struct CatalogPageRef { uint64_t page = 0; int64_t cap = 0; };
struct CatalogValue { std::string data; int64_t cap = 0; };
// Source and binding for one logical tablet. The cap lives on the COW path;
// the physical identity remains unchanged when a view is forked.
struct CatalogTabletSource {
  uint64_t table_id = 0;
  uint64_t physical_tablet_id = 0;
  int64_t create_transaction_id = 0;
  uint64_t data_tablet_id = 0;
  uint64_t lob_meta_tablet_id = 0;
  uint64_t lob_piece_tablet_id = 0;
  bool is_valid() const {
    return table_id != 0 && physical_tablet_id != 0
        && create_transaction_id > 0 && data_tablet_id != 0;
  }
};
// One physical incarnation and the oldest snapshot needed by the collected
// source views. This is transient GC work, never a Namespace ownership index.
using PhysicalRetention = storage::PhysicalSnapshotRequirement;
struct CatalogChange {
  CatalogValue value;
  bool erase = false;
};
using CatalogChanges = std::map<std::string, CatalogChange>;
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
  int64_t state = 0; // 0 LIVE, 1 DELETING, 2 DELETED; ids are never reused.
};

// Live read roots are independent of KV transaction lifetimes. A holder must
// be registered while its root-selection transaction still excludes page GC.
// Copying a holder extends protection; releasing the last copy removes it.
class NamespaceCatalogViews final {
  struct State;
public:
  struct Entry {
    uint64_t namespace_id;
    int64_t snapshot;
    CatalogRoots roots;
    // Opaque storage retention; copied GC entries keep historical pages alive
    // even if the last SQL View holder concurrently releases its registration.
    std::shared_ptr<const void> retention;
  };
  class View final {
  public:
    ~View();
    const Entry &entry() const { return entry_; }
  private:
    friend class NamespaceCatalogViews;
    View(std::shared_ptr<State> state, const Entry &entry);
    const std::shared_ptr<State> state_;
    const Entry entry_;
    View(const View &) = delete;
    View &operator=(const View &) = delete;
  };
  using Handle = std::shared_ptr<const View>;
  NamespaceCatalogViews();
  Handle hold(uint64_t namespace_id, int64_t snapshot, const CatalogRoots &roots,
              std::shared_ptr<const void> retention = {});
  // Share an already protected coordinator view with local parallel workers.
  // This never reopens a historical root after its last holder has released it.
  Handle find(uint64_t namespace_id, int64_t snapshot) const;
  void list(std::vector<Entry> &entries) const;
private:
  std::shared_ptr<State> state_;
  NamespaceCatalogViews(const NamespaceCatalogViews &) = delete;
  NamespaceCatalogViews &operator=(const NamespaceCatalogViews &) = delete;
};

class NamespaceCatalogCodec final {
public:
  static constexpr size_t PAGE_BYTES = 16 * 1024;
  static constexpr size_t MAX_KEY_BYTES = 512;
  static constexpr size_t OBJECT_CHUNK_BYTES = 60000;
  static constexpr size_t MAX_OBJECT_CHUNKS = (OBJECT_CHUNK_BYTES - 24) / 8;
  static int64_t cap_min(int64_t a, int64_t b);
  static std::string object_key(uint64_t id);
  static std::string encode_entry(uint64_t schema_object, uint64_t local_table,
                                  uint64_t local_tablet, uint64_t bound_tablet = 0);
  static bool decode_entry(const std::string &data, uint64_t &schema_object,
                           uint64_t &local_table, uint64_t &local_tablet,
                           uint64_t &bound_tablet);
  static std::string encode_source(const CatalogTabletSource &source);
  static bool decode_source(const std::string &data, CatalogTabletSource &source);
  static std::string encode_node(const CatalogNode &node);
  static bool decode_node(const std::string &data, int64_t cap, CatalogNode &node);
  static std::string encode_object(uint64_t size, const std::vector<uint64_t> &chunks);
  static bool decode_object(const std::string &data, uint64_t &size,
                             std::vector<uint64_t> &chunks);
};

// Page persistence belongs to the caller; the tree owns ordering, splits,
// snapshot caps, and immutable-page replacement.
class ICatalogPageStore {
public:
  virtual ~ICatalogPageStore() = default;
  virtual int read(uint64_t page, std::string &data) = 0;
  virtual int write(const std::string &data, uint64_t &page) = 0;
};

enum class CatalogTreeError : uint8_t { NONE, NOT_FOUND, CORRUPT, TOO_DEEP, STORE, INVALID, TOO_LARGE };
struct CatalogTreeResult {
  CatalogTreeError error = CatalogTreeError::NONE;
  int store_error = 0;
  bool ok() const { return error == CatalogTreeError::NONE; }
  static CatalogTreeResult from_store(int error) {
    return {CatalogTreeError::STORE, error};
  }
};

class NamespaceCatalogTree final {
public:
  explicit NamespaceCatalogTree(ICatalogPageStore &store) : store_(store) {}
  CatalogTreeResult read_node(CatalogPageRef ref, CatalogNode &node);
  // Merge source roots read through this store's fixed MVCC view. Shared pages
  // are revisited only when a path imposes an older cap. An error invalidates
  // the caller's whole workset; it must never publish a partial retention plan.
  CatalogTreeResult retain_sources(const std::vector<CatalogPageRef> &roots,
      int64_t snapshot, size_t max_entries,
      std::map<uint64_t, PhysicalRetention> &retained);
  CatalogTreeResult find(CatalogPageRef root, const std::string &key, CatalogValue &value);
  // Read at most limit ordered entries strictly after after. An empty after
  // starts at the first entry. The caller resumes by the last returned key;
  // no iterator or page remains pinned between batches. Output is empty on error.
  CatalogTreeResult scan(CatalogPageRef root, const std::string &after, size_t limit,
      std::vector<std::pair<std::string, CatalogValue>> &entries);
  CatalogTreeResult put(CatalogPageRef root, const std::string &key,
                        CatalogValue value, CatalogPageRef &next);
  CatalogTreeResult remove(CatalogPageRef root, const std::string &key,
                           CatalogPageRef &next);
  // Apply a complete transaction's ordered changes. Each affected old page is
  // visited once; only the final replacement pages are written. Missing erases
  // are harmless. The caller publishes next in the same page-store transaction,
  // and rolls that transaction back on failure. next is unchanged on failure.
  CatalogTreeResult apply(CatalogPageRef root, const CatalogChanges &changes,
                          CatalogPageRef &next);
private:
  struct PendingPage;
  struct Branch {
    CatalogPageRef ref;
    std::string lower;
    std::shared_ptr<PendingPage> pending;
  };
  // Lives only for one edit batch. Delay persistence until the final root is
  // known, including root collapse after deletion; no intermediate roots leak.
  struct PendingPage { CatalogNode node; std::vector<Branch> children; };
  using ChangeIterator = CatalogChanges::const_iterator;
  CatalogTreeResult save_node(const CatalogNode &node, CatalogPageRef &ref);
  CatalogTreeResult apply_path(CatalogPageRef root, const std::string &lower,
      ChangeIterator begin, ChangeIterator end, std::vector<Branch> &out, int depth);
  CatalogTreeResult stage_leaves(const CatalogNode &node, const std::string &lower,
                                std::vector<Branch> &out);
  CatalogTreeResult stage_branches(const std::vector<Branch> &children,
                                  std::vector<Branch> &out);
  CatalogTreeResult persist(Branch &branch, CatalogPageRef &ref, int depth);
  ICatalogPageStore &store_;
};

} // namespace ns
} // namespace oceanbase

#endif // OCEANBASE_NAMESPACE_CATALOG_H_
