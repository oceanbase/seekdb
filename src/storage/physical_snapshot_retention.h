#ifndef SEEKDB_PHYSICAL_SNAPSHOT_RETENTION_H_
#define SEEKDB_PHYSICAL_SNAPSHOT_RETENTION_H_

#include <algorithm>
#include <cstdint>
#include <map>

namespace oceanbase {
namespace storage {

struct PhysicalSnapshotRequirement {
  int64_t create_transaction_id = 0;
  int64_t snapshot = 0;
};

// An immutable, completely collected physical retention plan. read_snapshot
// identifies the consistent cut; new_source_floor bounds dependencies which
// can be published after that cut. Storage does not interpret logical owners.
struct PhysicalSnapshotRetention {
  int64_t read_snapshot = 0;
  int64_t new_source_floor = 0;
  std::map<uint64_t, PhysicalSnapshotRequirement> tablets;

  bool is_valid() const {
    return new_source_floor > 0 && read_snapshot >= new_source_floor;
  }
  // A mismatched incarnation is an invalid plan for this object, never an
  // absent dependency. Callers must stop reclamation on false.
  bool get(uint64_t tablet, int64_t create_transaction_id, int64_t &snapshot) const {
    if (!is_valid()) { return false; }
    snapshot = new_source_floor;
    const auto found = tablets.find(tablet);
    if (found != tablets.end()) {
      if (create_transaction_id <= 0
          || found->second.create_transaction_id != create_transaction_id
          || found->second.snapshot <= 0) { return false; }
      snapshot = std::min(snapshot, found->second.snapshot);
    }
    return true;
  }
};

} // namespace storage
} // namespace oceanbase
#endif
