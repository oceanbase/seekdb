/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */
#include "storage/compaction/physical_merge_candidate.h"
#include "storage/tablet/ob_tablet.h"
#include "share/ob_tablet_local_checksum_operator.h"

namespace oceanbase
{
namespace compaction
{
bool PhysicalMergeCandidate::matches(const share::ObTabletRuntimeInfo &report) const
{
  return is_live() && report.is_valid()
      && tablet_id == report.get_tablet_id().id()
      && create_transaction_id == report.get_create_transaction_id()
      && create_version == report.get_physical_create_version()
      && layout_id == report.get_storage_layout_id();
}

bool PhysicalMergeCandidate::matches(const share::ObTabletLocalChecksumItem &checksum) const
{
  return is_live() && checksum.is_valid()
      && tablet_id == checksum.tablet_id_.id()
      && create_transaction_id == checksum.create_transaction_id_
      && layout_id == checksum.storage_layout_id_;
}

int PhysicalMergeCandidate::load(const storage::ObTablet &tablet)
{
  using namespace common;
  using namespace storage;
  *this = PhysicalMergeCandidate();
  if (tablet.is_ls_inner_tablet()) { return OB_INVALID_ARGUMENT; }
  ObTabletCreateDeleteMdsUserData status;
  mds::MdsWriter writer;
  mds::TwoPhaseCommitState commit_state;
  share::SCN node_version;
  int ret = tablet.get_latest_tablet_status(status, writer, commit_state, node_version);
  if (ret == OB_SUCCESS) {
    tablet_id = tablet.get_tablet_id().id();
    layout_id = tablet.get_tablet_meta().storage_layout_id_;
    create_transaction_id = status.create_transaction_id_;
    create_version = status.physical_create_version_;
    // The optional node_version is absent after MDS checkpoint. C is the
    // persisted physical commit version, never the inherited source snapshot.
    if (commit_state == mds::TwoPhaseCommitState::ON_ABORT) {
      ret = OB_EAGAIN;
    } else if (create_version <= 0 || create_version == INT64_MAX) {
      if (commit_state != mds::TwoPhaseCommitState::ON_COMMIT
          && (status.data_type_ == ObTabletMdsUserDataType::CREATE_TABLET
              || status.data_type_ == ObTabletMdsUserDataType::PROTOTYPE_MATERIALIZE_TABLET)) {
        state = State::UNCOMMITTED;
      } else {
        ret = OB_STATE_NOT_MATCH;
      }
    } else if (create_transaction_id <= 0) {
      ret = OB_STATE_NOT_MATCH;
    } else if (status.tablet_status_ == ObTabletStatus::DELETED
        && commit_state == mds::TwoPhaseCommitState::ON_COMMIT) {
      state = State::RETIRED;
    } else if (layout_id == 0 || layout_id == OB_INVALID_ID) {
      ret = OB_STATE_NOT_MATCH;
    } else {
      // An uncommitted DELETE retains the previously committed creation.
      // It must not make that incarnation disappear from a pending round.
      state = State::LIVE;
    }
  }
  return ret;
}
} // namespace compaction
} // namespace oceanbase
