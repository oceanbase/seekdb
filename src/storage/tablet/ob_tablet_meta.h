/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OCEANBASE_STORAGE_TABLET_OB_TABLET_META
#define OCEANBASE_STORAGE_TABLET_OB_TABLET_META

#include "common/ob_tablet_id.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_fixed_array.h"
#include "lib/container/ob_se_array.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_template_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "share/ob_ddl_common.h"
#include "share/ob_tablet_autoincrement_param.h"
#include "storage/ob_storage_schema.h"
#include "storage/ob_storage_struct.h"
#include "storage/blocksstable/ob_sstable.h"
#include "storage/compaction/ob_medium_compaction_mgr.h"
#include "storage/ddl/ob_tablet_barrier_log.h"
#include "storage/tablet/ob_tablet_binding_helper.h"
#include "storage/tablet/ob_tablet_multi_source_data.h"
#include "storage/tablet/ob_tablet_mds_data.h"
#include "storage/tx/ob_trans_define.h"
#include "storage/ob_tablet_local_status.h"
#include "storage/tablet/ob_tablet_table_store_flag.h"
#include "share/scn.h"
#include "storage/tablet/ob_tablet_mds_data.h"
#include "storage/tablet/ob_tablet_create_delete_mds_user_data.h"
#include "storage/tablet/ob_tablet_space_usage.h"
namespace oceanbase
{
namespace storage
{
class ObTabletMeta final
{
  friend class ObTablet;
public:
  static const share::SCN INIT_CLOG_CHECKPOINT_SCN;
  static const share::SCN INVALID_CREATE_SCN;
  static const share::SCN INIT_CREATE_SCN;

public:
  ObTabletMeta();
  ObTabletMeta(const ObTabletMeta &other) = delete;
  ObTabletMeta &operator=(const ObTabletMeta &other) = delete;
  ~ObTabletMeta();
public:
  // first init func
  int init(
      const common::ObTabletID &tablet_id,
      const common::ObTabletID &data_tablet_id,
      const share::SCN create_scn,
      const int64_t snapshot_version,
      const ObTabletTableStoreFlag &table_store_flag,
      const int64_t create_schema_version,
      const share::SCN &clog_checkpoint_scn,
      const share::SCN &mds_checkpoint_scn,
      const bool micro_index_clustered,
      const bool has_truncate_info,
      const share::ObForkTabletInfo &fork_info = share::ObForkTabletInfo());
  int init(
      const ObTabletMeta &old_tablet_meta,
      const int64_t snapshot_version,
      const int64_t multi_version_start,
      const int64_t max_sync_storage_schema_version,
      const share::SCN clog_checkpoint_scn = share::SCN::min_scn(),
      const ObDDLTableStoreParam &ddl_info = ObDDLTableStoreParam(),
      const bool has_truncate_info = false);
  int init(
      const ObTabletMeta &old_tablet_meta,
      const int64_t snapshot_version,
      const int64_t multi_version_start,
      const int64_t max_sync_storage_schema_version,
      const share::SCN &clog_checkpoint_scn,
      const share::SCN &mds_checkpoint_scn,
      const share::ObForkTabletInfo &fork_info);
  int init(
      const ObTabletMeta &old_tablet_meta,
      const share::SCN &flush_scn);
  int assign(const ObTabletMeta &other);
  void reset();
  bool is_valid() const;

  // serialize & deserialize
  int serialize(char *buf, const int64_t len, int64_t &pos) const;
  int deserialize(
      const char *buf,
      const int64_t len,
      int64_t &pos);
  int64_t get_serialize_size() const;
  share::SCN get_ddl_sstable_start_scn() const;
  // Return the max replayed scn which is the max scn among clog_checkpoint_scn,
  // mds_checkpoint_scn and ddl_checkpoint_scn.
  // Note, if a new type of checkpoint scn is added, donot forget to modify the returned scn.
  share::SCN get_max_replayed_scn() const;
public:
  static int init_report_info(
      const blocksstable::ObSSTable *sstable,
      const int64_t report_version,
      ObTabletReportStatus &report_status);
  static int update_meta_last_persisted_committed_tablet_status(
    const ObTabletTxMultiSourceDataUnit &tx_data,
    const share::SCN &create_commit_scn,
    ObTabletCreateDeleteMdsUserData &last_persisted_committed_tablet_status);
public:
  TO_STRING_KV(K_(version),
               K_(tablet_id),
               K_(data_tablet_id),
               K_(ref_tablet_id),
               K_(has_next_tablet),
               K_(create_scn),
               K_(start_scn),
               K_(clog_checkpoint_scn),
               K_(ddl_checkpoint_scn),
               K_(snapshot_version),
               K_(multi_version_start),
               K_(compat_mode),
               K_(local_status),
               K_(report_status),
               K_(table_store_flag),
               K_(ddl_start_scn),
               K_(ddl_snapshot_version),
               K_(max_sync_storage_schema_version),
               K_(max_serialized_medium_scn),
               K_(ddl_execution_id),
               K_(ddl_data_format_version),
               K_(ddl_commit_scn),
               K_(mds_checkpoint_scn),
               K_(extra_medium_info),
               K_(last_persisted_committed_tablet_status),
               K_(create_schema_version),
               K_(space_usage),
               K_(micro_index_clustered),
               K_(fork_info),
               K_(has_truncate_info));

public:
  int32_t version_; // alignment: 4B, size: 4B
  int32_t length_; // alignment: 4B, size: 4B
  common::ObTabletID tablet_id_; // alignment: 8B, size: 8B
  common::ObTabletID data_tablet_id_; // alignment: 8B, size: 8B
  common::ObTabletID ref_tablet_id_; // alignment: 8B, size: 8B
  share::SCN create_scn_; // alignment: 8B, size: 8B create_tablet_scn, not create_tablet_version_scn
  share::SCN start_scn_; // alignment: 8B, size: 8B
  share::SCN clog_checkpoint_scn_; // may less than last_minor->end_log_ts. alignment: 8B, size: 8B
  share::SCN ddl_checkpoint_scn_; // alignment: 8B, size: 8B
  int64_t snapshot_version_; // alignment: 8B, size: 8B
  int64_t multi_version_start_; // alignment: 8B, size: 8B
  ObTabletLocalStatus local_status_; // alignment: 8B, size: 8B
  ObTabletReportStatus report_status_; // alignment: 8B, size: 40B
  ObTabletTableStoreFlag table_store_flag_; // alignment: 8B, size: 8B
  share::SCN ddl_start_scn_; // alignment: 8B, size: 8B
  int64_t ddl_snapshot_version_; // alignment: 8B, size: 8B
  // max_sync_storage_schema_version_ = MIN(serialized_schema_version, sync_schema_version)
  // serialized_schema_version > sync_schema_version when major update storage schema
  // sync_schema_version > serialized_schema_version when replay schema clog but not mini merge yet
  // max_sync_storage_schema_version will be inaccurate after 4.2
  int64_t max_sync_storage_schema_version_; // alignment: 8B, size: 8B
  int64_t ddl_execution_id_; // alignment: 8B, size: 8B
  int64_t ddl_data_format_version_; // alignment: 8B, size: 8B
  int64_t max_serialized_medium_scn_; // abandon after 4.2 // alignment: 8B, size: 8B
  share::SCN ddl_commit_scn_; // alignment: 8B, size: 8B
  share::SCN mds_checkpoint_scn_; // alignment: 8B, size: 8B
  share::SCN min_ss_tablet_version_; // alignment: 8B, size: 8B
  compaction::ObExtraMediumInfo extra_medium_info_;
  ObTabletCreateDeleteMdsUserData last_persisted_committed_tablet_status_; // quick access for tablet status in sstables
  ObTabletSpaceUsage space_usage_; // alignment: 8B, size: 48B
  int64_t create_schema_version_;
  lib::Worker::CompatMode compat_mode_; // alignment: 1B, size: 4B
  bool has_next_tablet_; // alignment: 1B, size: 2B
  bool is_empty_shell_; // alignment: 1B, size: 2B
  bool micro_index_clustered_; // alignment: 1B, size: 2B
  share::ObForkTabletInfo fork_info_; // alignment: 8B, size: 24B
  bool has_truncate_info_; // be True after first major with truncate info
private:
  void update_extra_medium_info(
      const compaction::ObMergeType merge_type,
      const int64_t finish_medium_scn);
  inline void set_space_usage_ (const ObTabletSpaceUsage &space_usage) { space_usage_ = space_usage; }
private:
  static const int32_t TABLET_META_VERSION = 2;
private:
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_TABLET_OB_TABLET_META
