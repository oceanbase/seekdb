/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "rootserver/freeze/namespace_freeze_preparation.h"
#include "rootserver/fork_table/instance_namespace_metadata.h"
#include "storage/compaction/physical_merge_candidate.h"
#include "storage/tablet/ob_tablet_create_delete_helper.h"
#include "storage/tablet/ob_tablet.h"
#include "lib/time/ob_time_utility.h"
#include "namespace/namespace.h"

namespace oceanbase {
namespace rootserver {
using namespace common;
using namespace storage;

int NamespaceFreezePreparation::check(InstanceMetaStore &store, int64_t deadline, Status &status)
{
  status = Status();
  InstanceMetaStore::Transaction view;
  int ret = store.begin(view, deadline, true);
  InstanceNamespaceMetadata metadata(store, view);
  std::vector<InstanceNamespaceRecord> live;
  if (ret == OB_SUCCESS) {
    ret = metadata.scan_namespaces([&](const InstanceNamespaceRecord &record) {
      if (record.roots.state == 0) { live.push_back(record); }
      return OB_SUCCESS;
    });
  }
  InstanceCatalogPageStore pages(metadata);
  ns::NamespaceCatalogTree tree(pages);
  for (const auto &record : live) {
    if (ret != OB_SUCCESS || !status.ready) { break; }
    std::string position;
    bool end = false;
    while (ret == OB_SUCCESS && status.ready && !end) {
      if (ObTimeUtility::current_time() >= deadline) { ret = OB_TIMEOUT; break; }
      std::vector<std::pair<std::string, ns::CatalogValue>> entries;
      const auto scanned = tree.scan(record.roots.directory, position, 64, entries);
      if (!scanned.ok()) {
        ret = scanned.error == ns::CatalogTreeError::STORE ? scanned.store_error : OB_CHECKSUM_ERROR;
        break;
      }
      for (const auto &entry : entries) {
        ns::CatalogTabletSource source;
        if (!ns::NamespaceCatalogCodec::decode_source(entry.second.data, source)) {
          ret = OB_CHECKSUM_ERROR;
          break;
        }
        ++status.inspected_tablets;
        const bool inherited = entry.second.cap != 0
            || ns::NamespaceObjectKey::encoded_namespace(source.physical_tablet_id) != record.id;
        bool complete = false;
        if (!inherited) {
          ObTabletHandle handle;
          compaction::PhysicalMergeCandidate physical;
          ret = ObTabletCreateDeleteHelper::check_and_get_tablet(
              ObTabletMapKey(ObTabletID(source.physical_tablet_id)), handle, 0,
              ObMDSGetTabletMode::READ_WITHOUT_CHECK, transaction::ObTransVersion::MAX_TRANS_VERSION);
          if (ret == OB_SUCCESS) { ret = physical.load(*handle.get_obj()); }
          if (ret == OB_SUCCESS && physical.create_transaction_id != source.create_transaction_id) {
            ret = OB_STATE_NOT_MATCH;
          }
          if (ret == OB_SUCCESS) {
            const auto &tablet = *handle.get_obj();
            const auto &fork = tablet.get_tablet_meta().fork_info_;
            complete = physical.is_live() && tablet.is_data_complete()
                && (!fork.is_valid() || fork.is_complete());
          }
        }
        if (ret != OB_SUCCESS || !complete) {
          status.ready = false;
          status.namespace_id = record.id;
          status.tablet_id = source.physical_tablet_id;
          status.needs_materialization = inherited;
          break;
        }
        position = entry.first;
      }
      end = entries.size() < 64;
    }
  }
  if (view.is_active()) {
    const int end = store.rollback(view);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  if (ret != OB_SUCCESS) { status.ready = false; }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
