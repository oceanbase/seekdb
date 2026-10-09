/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef OCEANBASE_COMPACTION_PHYSICAL_MERGE_CANDIDATE_H_
#define OCEANBASE_COMPACTION_PHYSICAL_MERGE_CANDIDATE_H_

#include <stdint.h>

namespace oceanbase
{
namespace storage { class ObTablet; }
namespace share { class ObTabletRuntimeInfo; struct ObTabletLocalChecksumItem; }
namespace compaction
{
// A physical incarnation, independent of SQL catalog ownership. Enumerators
// must reach the target's native commit/replay horizon before loading objects.
struct PhysicalMergeCandidate final
{
  enum class State { UNKNOWN, UNCOMMITTED, LIVE, RETIRED };
  State state = State::UNKNOWN;
  uint64_t tablet_id = 0;
  uint64_t layout_id = 0;
  int64_t create_transaction_id = 0;
  int64_t create_version = 0;

  int load(const storage::ObTablet &tablet);
  bool is_live() const { return state == State::LIVE; }
  bool participates(int64_t target) const { return is_live() && create_version <= target; }
  bool matches(const share::ObTabletRuntimeInfo &report) const;
  bool matches(const share::ObTabletLocalChecksumItem &checksum) const;
};
} // namespace compaction
} // namespace oceanbase
#endif
