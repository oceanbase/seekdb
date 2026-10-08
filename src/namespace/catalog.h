#ifndef OCEANBASE_NAMESPACE_CATALOG_H_
#define OCEANBASE_NAMESPACE_CATALOG_H_

// Persistent namespace catalog values and their storage-independent encoding.
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
  uint64_t snapshot_ref = 0;
  uint64_t parent_ref = 0; int64_t ref_count = 0; // Canonical snapshot rows only.
  int64_t state = 0; // 0 LIVE, 1 DELETING, 2 DELETED; ids are never reused.
  int64_t active_schema_changes = 0;
  int64_t pending_schema_version = 0;
  bool valid_snapshot(uint64_t expected_id) const;
};

// The caller owns one transaction for the whole release. Each load locks a
// snapshot row, and removing a row also removes its persistent snapshot pin.
class ISnapshotLineageStore {
public:
  virtual ~ISnapshotLineageStore() = default;
  virtual int load_for_update(uint64_t snapshot_id, CatalogRoots &roots) = 0;
  virtual int increment_ref(uint64_t snapshot_id) = 0;
  virtual int decrement_ref(uint64_t snapshot_id) = 0;
  virtual int insert_snapshot(const CatalogRoots &roots) = 0;
  virtual int attach_child(uint64_t child_id, uint64_t parent_namespace_id,
                           const CatalogRoots &roots) = 0;
  virtual int remove_snapshot(uint64_t snapshot_id, const CatalogRoots &roots) = 0;
};

enum class SnapshotForkError : uint8_t { NONE, INVALID, OVERFLOW, STORE };
struct SnapshotForkResult {
  SnapshotForkError error = SnapshotForkError::NONE;
  int store_error = 0;
};

class NamespaceSnapshotLineage final {
public:
  static SnapshotForkResult fork(uint64_t parent_namespace_id, uint64_t child_id,
                                 CatalogRoots &roots, ISnapshotLineageStore &store);
  static int release(uint64_t snapshot_id, ISnapshotLineageStore &store);
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
  CatalogTreeResult find(CatalogPageRef root, const std::string &key, CatalogValue &value);
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

struct NamespaceExceptionRow {
  uint64_t tablet = 0;
  uint64_t table = 0;
  int64_t kind = 0;
};

enum class ExceptionDeltaKind : uint8_t { TOMBSTONE, PROBE_NEW, UPDATE_TABLE };
struct ExceptionDeltaAction {
  ExceptionDeltaKind kind;
  uint64_t tablet_id;
  uint64_t table_id;
};

class NamespaceExceptionDelta final {
public:
  // The maps contain namespace-local tablet id -> table id. A new tablet
  // still needs a physical-presence probe before it can be marked owned.
  static std::vector<ExceptionDeltaAction> plan(
      const std::map<uint64_t, uint64_t> &previous,
      const std::map<uint64_t, uint64_t> &current);
};

class IExceptionLoader {
public:
  virtual ~IExceptionLoader() = default;
  // Return zero on success; the caller's storage error passes through unchanged.
  class IRowSink {
  public:
    virtual ~IRowSink() = default;
    virtual void add(const NamespaceExceptionRow &row) = 0;
  };
  virtual int load(uint64_t namespace_id, IRowSink &sink) = 0;
};

// One namespace's immutable lineage links and committed tablet exceptions.
// Registry owns this state so there is still only one process-wide namespace
// authority. Loading, post-commit insertions and invalidation share a lock,
// so an older load cannot republish a snapshot after its invalidation.
class NamespaceControlState final {
public:
  bool chain_link(uint64_t namespace_id, uint64_t &parent, int64_t &fork_cap) const;
  void remember_chain_link(uint64_t namespace_id, uint64_t parent, int64_t fork_cap);
  void forget_chain_link(uint64_t namespace_id);
  int load_exceptions(uint64_t namespace_id, IExceptionLoader &loader, bool reload = false);
  void clear_exceptions();
  bool owned(uint64_t namespace_id, uint64_t local_tablet,
             uint64_t *table = nullptr) const;
  bool tombstoned(uint64_t namespace_id, uint64_t local_tablet) const;
  void apply_owned(uint64_t namespace_id, uint64_t local_tablet, uint64_t table);
  void drop_exceptions(uint64_t namespace_id);
private:
  struct ExceptionSet {
    bool loaded = false;
    std::unordered_map<uint64_t, uint64_t> owned;
    std::unordered_set<uint64_t> tombstoned;
  };
  mutable std::shared_mutex chain_mutex_;
  std::unordered_map<uint64_t, std::pair<uint64_t, int64_t>> chain_links_;
  mutable std::mutex exceptions_mutex_;
  std::unordered_map<uint64_t, ExceptionSet> exception_sets_;
};

} // namespace ns
} // namespace oceanbase

#endif // OCEANBASE_NAMESPACE_CATALOG_H_
