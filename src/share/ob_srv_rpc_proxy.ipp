#include "rpc/obrpc/ob_rpc_proxy_macros.h"

  // special usage when can't deliver request.
  RPC_S(PR5 nouse, OB_ERROR_PACKET);

  RPC_S(PR5 set_config, OB_SET_CONFIG, (common::ObString));
  RPC_S(PR5 get_config, OB_GET_CONFIG, common::ObString);
  RPC_S(PR5 set_tenant_config, OB_SET_TENANT_CONFIG, (obrpc::ObTenantConfigArg));
  RPC_S(PR1 get_diagnose_args, OB_GET_DIAGNOSE_ARGS, common::ObString);


  RPC_AP(PR5 get_wrs_info, OB_GET_WRS_INFO, (ObGetWRSArg), ObGetWRSResult);
  RPC_AP(PR5 stop_write, OB_PARTITION_STOP_WRITE, (obrpc::Int64), obrpc::Int64);
  RPC_AP(PR5 check_log, OB_PARTITION_CHECK_LOG, (obrpc::Int64), obrpc::Int64);
  RPC_AP(PR5 check_frozen_scn, OB_CHECK_FROZEN_SCN, (obrpc::ObCheckFrozenScnArg));
  RPC_AP(PR5 get_min_sstable_schema_version, OB_GET_MIN_SSTABLE_SCHEMA_VERSION,
      (obrpc::ObGetMinSSTableSchemaVersionArg), obrpc::ObGetMinSSTableSchemaVersionRes);
  RPC_AP(PR5 init_tenant_config, OB_INIT_TENANT_CONFIG,
      (obrpc::ObInitTenantConfigArg), obrpc::ObInitTenantConfigRes);
  // Rpc bulk interface for replication, migration, etc.
  RPC_S(PR5 delete_backup_ls_task, OB_DELETE_BACKUP_LS_TASK, (ObLSBackupCleanArg));
  RPC_S(PR5 backup_ls_data, OB_BACKUP_LS_DATA, (ObBackupDataArg));
  RPC_S(PR5 backup_completing_log, OB_BACKUP_COMPL_LOG, (ObBackupComplLogArg));
  RPC_S(PR5 backup_build_index, OB_BACKUP_BUILD_INDEX, (ObBackupBuildIdxArg));
  RPC_S(PR5 backup_meta, OB_BACKUP_META, (ObBackupMetaArg));
  RPC_S(PR5 backup_fuse_tablet_meta, OB_BACKUP_FUSE_TABLET_META, (ObBackupFuseTabletMetaArg));
  RPC_S(PR5 check_backup_task_exist, OB_CHECK_BACKUP_TASK_EXIST, (ObBackupCheckTaskArg), obrpc::Bool);
  RPC_S(PR5 report_backup_over, OB_BACKUP_LS_DATA_RES, (ObBackupTaskRes));
  RPC_S(PR5 report_backup_clean_over, OB_DELETE_BACKUP_LS_TASK_RES, (ObBackupTaskRes));
  RPC_S(PR5 notify_archive, OB_NOTIFY_ARCHIVE, (ObNotifyArchiveArg));
  RPC_S(PR5 checkpoint_slog, OB_CHECKPOINT_SLOG, (ObCheckpointSlogArg));

  RPC_AP(PR5 minor_freeze, OB_MINOR_FREEZE, (ObMinorFreezeArg), obrpc::Int64);
  RPC_AP(PR5 check_schema_version_elapsed, OB_CHECK_SCHEMA_VERSION_ELAPSED, (ObCheckSchemaVersionElapsedArg), ObCheckSchemaVersionElapsedResult);
  RPC_AP(PR5 check_memtable_cnt, OB_CHECK_MEMTABLE_CNT, (ObCheckMemtableCntArg), ObCheckMemtableCntResult);
  RPC_AP(PR5 check_medium_compaction_info_list_cnt, OB_CHECK_MEDIUM_INFO_LIST_CNT, (ObCheckMediumCompactionInfoListArg), ObCheckMediumCompactionInfoListResult);
  RPC_AP(PR5 check_modify_time_elapsed, OB_CHECK_MODIFY_TIME_ELAPSED, (ObCheckModifyTimeElapsedArg), ObCheckModifyTimeElapsedResult);

  RPC_AP(PR5 check_ddl_tablet_merge_status, OB_DDL_CHECK_TABLET_MERGE_STATUS, (ObDDLCheckTabletMergeStatusArg), ObDDLCheckTabletMergeStatusResult);
  RPC_S(PR5 switch_leader, OB_SWITCH_LEADER, (ObSwitchLeaderArg));
  RPC_S(PR5 batch_switch_rs_leader, OB_BATCH_SWITCH_RS_LEADER, (ObAddr));
  RPC_S(PR5 get_partition_count, OB_GET_PARTITION_COUNT,
        ObGetPartitionCountResult);
  RPC_AP(PR5 switch_schema, OB_SWITCH_SCHEMA, (ObSwitchSchemaArg), obrpc::ObSwitchSchemaResult);
  RPC_S(PR5 refresh_memory_stat, OB_REFRESH_MEMORY_STAT);
  RPC_S(PR5 wash_memory_fragmentation, OB_WASH_MEMORY_FRAGMENTATION);
  RPC_S(PR5 recycle_replica, OB_RECYCLE_REPLICA);
  RPC_S(PR5 clear_location_cache, OB_CLEAR_LOCATION_CACHE);
  RPC_S(PR5 refresh_sync_value, OB_REFRESH_SYNC_VALUE, (ObAutoincSyncArg));
  RPC_S(PR5 sync_auto_increment, OB_SYNC_AUTO_INCREMENT, (ObAutoincSyncArg));
  RPC_S(PR5 clear_autoinc_cache, OB_CLEAR_AUTOINC_CACHE, (ObAutoincSyncArg));
  RPC_S(PR5 dump_memtable, OB_DUMP_MEMTABLE, (ObDumpMemtableArg));
  RPC_S(PR5 dump_tx_data_memtable, OB_DUMP_TX_DATA_MEMTABLE, (ObDumpTxDataMemtableArg));
  RPC_S(PR5 dump_single_tx_data, OB_DUMP_SINGLE_TX_DATA, (ObDumpSingleTxDataArg));
  RPC_S(PR5 halt_all_prewarming, OB_FORCE_PURGE_MEMTABLE);
  RPC_AP(PR5 halt_all_prewarming_async, OB_FORCE_PURGE_MEMTABLE_ASYNC, (obrpc::UInt64));
  RPC_S(PR5 set_debug_sync_action, OB_SET_DS_ACTION, (obrpc::ObDebugSyncActionArg));
  RPC_S(PR5 refresh_io_calibration, OB_REFRESH_IO_CALIBRATION, (obrpc::ObRefreshIOCalibrationArg));
  RPC_S(PR5 update_baseline_schema_version, OB_UPDATE_BASELINE_SCHEMA_VERSION, (obrpc::Int64));
  RPC_S(PR5 sync_partition_table, OB_SYNC_PARTITION_TABLE, (obrpc::Int64));
  RPC_S(PR5 flush_cache, OB_FLUSH_CACHE, (ObFlushCacheArg));
  RPC_S(PR5 set_tracepoint, OB_SET_TP, (obrpc::ObAdminSetTPArg));
  RPC_S(PR5 kill_session, OB_KILL_SESSION, (sql::ObKillSessionArg));
  RPC_S(PR5 cancel_sys_task, OB_CANCEL_SYS_TASK, (obrpc::ObCancelTaskArg));
  RPC_S(PR5 set_disk_valid, OB_SET_DISK_VALID, (ObSetDiskValidArg));
  RPC_S(PR5 add_disk, OB_ADD_DISK, (ObAdminAddDiskArg));
  RPC_S(PR5 drop_disk, OB_DROP_DISK, (ObAdminDropDiskArg));
  RPC_S(PR5 force_switch_ilog_file, OB_FORCE_SWITCH_ILOG_FILE, (ObForceSwitchILogFileArg));
  RPC_S(PR5 force_set_all_as_single_replica, OB_FORCE_SET_ALL_AS_SINGLE_REPLICA, (ObForceSetAllAsSingleReplicaArg));
  RPC_S(PR5 force_set_server_list, OB_FORCE_SET_SERVER_LIST, (ObForceSetServerListArg), obrpc::ObForceSetServerListResult);
  RPC_S(PR5 calc_column_checksum_request, OB_CALC_COLUMN_CHECKSUM_REQUEST, (ObCalcColumnChecksumRequestArg), obrpc::ObCalcColumnChecksumRequestRes);
  RPC_AP(PR5 build_ddl_single_replica_request, OB_DDL_BUILD_SINGLE_REPLICA_REQUEST, (obrpc::ObDDLBuildSingleReplicaRequestArg), obrpc::ObDDLBuildSingleReplicaRequestResult);
  RPC_S(PR5 build_split_tablet_data_start_request, OB_SPLIT_TABLET_DATA_START_REQUEST, (obrpc::ObTabletSplitStartArg), obrpc::ObTabletSplitStartResult);
  RPC_S(PR5 build_split_tablet_data_finish_request, OB_SPLIT_TABLET_DATA_FINISH_REQUEST, (obrpc::ObTabletSplitFinishArg), obrpc::ObTabletSplitFinishResult);
  RPC_S(PR5 freeze_split_src_tablet, OB_FREEZE_SPLIT_SRC_TABLET, (obrpc::ObFreezeSplitSrcTabletArg), obrpc::ObFreezeSplitSrcTabletRes);
  RPC_S(PR5 prepare_tablet_split_task_ranges, OB_PREPARE_TABLET_SPLIT_TASK_RANGES, (obrpc::ObPrepareSplitRangesArg), obrpc::ObPrepareSplitRangesRes);
  RPC_S(PR5 check_and_cancel_ddl_complement_dag, OB_CHECK_AND_CANCEL_DDL_COMPLEMENT_DAG, (ObDDLBuildSingleReplicaRequestArg), Bool);
  RPC_S(PR5 fetch_split_tablet_info, OB_FETCH_SPLIT_TABLET_INFO, (obrpc::ObFetchSplitTabletInfoArg), obrpc::ObFetchSplitTabletInfoRes);
  RPC_S(PR5 check_and_cancel_delete_lob_meta_row_dag, OB_CHECK_AND_CANCEL_DELETE_LOB_META_ROW_DAG, (ObDDLBuildSingleReplicaRequestArg), Bool);
  RPC_S(PR5 fetch_tablet_autoinc_seq_cache, OB_FETCH_TABLET_AUTOINC_SEQ_CACHE, (obrpc::ObFetchTabletSeqArg), obrpc::ObFetchTabletSeqRes);
  RPC_AP(PR5 batch_get_tablet_autoinc_seq, OB_BATCH_GET_TABLET_AUTOINC_SEQ, (obrpc::ObBatchGetTabletAutoincSeqArg), obrpc::ObBatchGetTabletAutoincSeqRes);
  RPC_AP(PR5 batch_set_tablet_autoinc_seq, OB_BATCH_SET_TABLET_AUTOINC_SEQ, (obrpc::ObBatchSetTabletAutoincSeqArg), obrpc::ObBatchSetTabletAutoincSeqRes);
  RPC_S(PR5 set_tablet_autoinc_seq, OB_SET_TABLET_AUTOINC_SEQ, (obrpc::ObBatchSetTabletAutoincSeqArg), obrpc::ObBatchSetTabletAutoincSeqRes);
  RPC_AP(PR5 clear_tablet_autoinc_seq_cache, OB_CLEAR_TABLET_AUTOINC_SEQ_CACHE, (obrpc::ObClearTabletAutoincSeqCacheArg), obrpc::Int64);
  RPC_S(PR5 batch_get_tablet_binding, OB_BATCH_GET_TABLET_BINDING, (obrpc::ObBatchGetTabletBindingArg), obrpc::ObBatchGetTabletBindingRes);
  RPC_S(PR5 batch_get_tablet_split, OB_BATCH_GET_TABLET_SPLIT, (obrpc::ObBatchGetTabletSplitArg), obrpc::ObBatchGetTabletSplitRes);
  RPC_S(PRD force_create_sys_table, OB_FORCE_CREATE_SYS_TABLE, (ObForceCreateSysTableArg));
  RPC_S(PRD schema_revise, OB_SCHEMA_REVISE, (ObSchemaReviseArg));
  RPC_S(PR5 force_disable_blacklist, OB_FORCE_DISABLE_BLACKLIST);
  RPC_S(PR5 force_enable_blacklist, OB_FORCE_ENABLE_BLACKLIST);
  RPC_S(PR5 force_clear_srv_blacklist, OB_FORCE_CLEAR_BLACKLIST);

  RPC_S(PR5 update_local_stat_cache, obrpc::OB_SERVER_UPDATE_STAT_CACHE, (ObUpdateStatCacheArg));
  // The optimizer estimates the number of rows
  RPC_S(PR5 estimate_partition_rows, OB_ESTIMATE_PARTITION_ROWS, (ObEstPartArg), ObEstPartRes);

  RPC_AP(PR1 ha_gts_ping_request, OB_HA_GTS_PING_REQUEST, (ObHaGtsPingRequest), ObHaGtsPingResponse);
  RPC_AP(PR1 ha_gts_get_request, OB_HA_GTS_GET_REQUEST, (ObHaGtsGetRequest));
  RPC_AP(PR1 ha_gts_get_response, OB_HA_GTS_GET_RESPONSE, (ObHaGtsGetResponse));
  RPC_AP(PR1 ha_gts_heartbeat, OB_HA_GTS_HEARTBEAT, (ObHaGtsHeartbeat));
  RPC_S(PR1 ha_gts_update_meta, OB_HA_GTS_UPDATE_META, (ObHaGtsUpdateMetaRequest),
      ObHaGtsUpdateMetaResponse);
  RPC_S(PR1 ha_gts_change_member, OB_HA_GTS_CHANGE_MEMBER, (ObHaGtsChangeMemberRequest),
      ObHaGtsChangeMemberResponse);
  RPC_S(PR5 get_tenant_refreshed_schema_version, OB_GET_TENANT_REFRESHED_SCHEMA_VERSION,
        (ObGetTenantSchemaVersionArg), ObGetTenantSchemaVersionResult);
  RPC_S(PR5 pre_process_server_status, OB_PRE_PROCESS_SERVER, (obrpc::ObPreProcessServerArg));
  RPC_S(PR5 handle_part_trans_ctx, OB_HANDLE_PART_TRANS_CTX, (obrpc::ObTrxToolArg), ObTrxToolRes);
  RPC_S(PR5 flush_local_opt_stat_monitoring_info, obrpc::OB_SERVER_FLUSH_OPT_STAT_MONITORING_INFO, (obrpc::ObFlushOptStatArg));
  RPC_AP(PR5 create_tablet, OB_CREATE_TABLET, (obrpc::ObBatchCreateTabletArg), obrpc::ObCreateTabletBatchRes);
  RPC_AP(PR5 drop_tablet, OB_DROP_TABLET, (obrpc::ObBatchRemoveTabletArg), obrpc::ObRemoveTabletRes);
  RPC_AP(PR5 lock_table, OB_TABLE_LOCK_TASK, (transaction::tablelock::ObTableLockTaskRequest),
         transaction::tablelock::ObTableLockTaskResult);
  RPC_AP(PR4 unlock_table, OB_HIGH_PRIORITY_TABLE_LOCK_TASK, (transaction::tablelock::ObTableLockTaskRequest),
         transaction::tablelock::ObTableLockTaskResult);
  RPC_AP(PR5 batch_lock_obj, OB_BATCH_TABLE_LOCK_TASK, (transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObLockParam>),
         transaction::tablelock::ObTableLockTaskResult);
  RPC_AP(PR4 batch_unlock_obj, OB_HIGH_PRIORITY_BATCH_TABLE_LOCK_TASK, (transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObLockParam>),
         transaction::tablelock::ObTableLockTaskResult);
  RPC_AP(PR5 batch_replace_lock_obj, OB_BATCH_REPLACE_TABLE_LOCK_TASK, (transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObReplaceLockParam>),
         transaction::tablelock::ObTableLockTaskResult);
  RPC_S(PR4 admin_remove_lock_op, OB_REMOVE_OBJ_LOCK, (transaction::tablelock::ObAdminRemoveLockOpArg));
  RPC_S(PR4 admin_update_lock_op, OB_UPDATE_OBJ_LOCK, (transaction::tablelock::ObAdminUpdateLockOpArg));
  RPC_S(PR5 remote_write_ddl_redo_log, OB_REMOTE_WRITE_DDL_REDO_LOG, (obrpc::ObRpcRemoteWriteDDLRedoLogArg));
  RPC_S(PR5 remote_write_ddl_commit_log, OB_REMOTE_WRITE_DDL_COMMIT_LOG, (obrpc::ObRpcRemoteWriteDDLCommitLogArg), obrpc::Int64);
  #ifdef OB_BUILD_SHARED_STORAGE
  RPC_S(PR5 remote_write_ddl_finsih_log, OB_REMOTE_WRITE_DDL_FINISH_LOG, (obrpc::ObRpcRemoteWriteDDLFinishLogArg));
  RPC_S(PR5 get_ss_macro_block, OB_GET_SS_MACRO_BLOCK, (obrpc::ObGetSSMacroBlockArg), obrpc::ObGetSSMacroBlockResult);
  RPC_S(PR5 sync_hot_micro_key, OB_SYNC_HOT_MICRO_KEY, (obrpc::ObLSSyncHotMicroKeyArg));
  RPC_S(PR5 get_ss_phy_block_info, OB_GET_SS_PHY_BLOCK_INFO, (obrpc::ObGetSSPhyBlockInfoArg), obrpc::ObGetSSPhyBlockInfoResult);
  RPC_S(PR5 get_ss_micro_block_meta, OB_GET_SS_MICRO_BLOCK_META, (obrpc::ObGetSSMicroBlockMetaArg), obrpc::ObGetSSMicroBlockMetaResult);
  RPC_S(PR5 get_ss_macro_block_by_uri, OB_GET_SS_MACRO_BLOCK_BY_URI, (obrpc::ObGetSSMacroBlockByURIArg), obrpc::ObGetSSMacroBlockByURIResult);
  RPC_S(PR5 del_ss_tablet_meta, OB_DEL_SS_TABLET_META, (obrpc::ObDelSSTabletMetaArg));
  RPC_S(PR5 enable_ss_micro_cache, OB_ENABLE_SS_MICRO_CACHE, (obrpc::ObEnableSSMicroCacheArg));
  RPC_S(PR5 get_ss_micro_cache_info, OB_GET_SS_MICRO_CACHE_INFO, (obrpc::ObGetSSMicroCacheInfoArg), obrpc::ObGetSSMicroCacheInfoResult);
  RPC_S(PR5 clear_ss_micro_cache, OB_CLEAR_SS_MICRO_CACHE, (obrpc::ObClearSSMicroCacheArg));
  RPC_S(PR5 del_ss_local_tmpfile, OB_DEL_SS_LOCAL_TMPFILE, (obrpc::ObDelSSLocalTmpFileArg));
  RPC_S(PR5 del_ss_local_major, OB_DEL_SS_LOCAL_MAJOR, (obrpc::ObDelSSLocalMajorArg));
  RPC_S(PR5 calibrate_ss_disk_space, OB_CALIBRATE_SS_DISK_SPACE, (obrpc::ObCalibrateSSDiskSpaceArg));
  RPC_S(PR5 del_ss_tablet_micro, OB_DEL_SS_TABLET_MICRO, (obrpc::ObDelSSTabletMicroArg));
  RPC_S(PR5 set_ss_ckpt_compressor, OB_SET_SS_CKPT_COMPRESSOR, (obrpc::ObSetSSCkptCompressorArg));
  RPC_S(PR5 set_ss_cache_size_ratio, OB_SET_SS_CACHE_SIZE_RATIO, (obrpc::ObSetSSCacheSizeRatioArg));
  RPC_S(PR5 trigger_storage_cache, OB_TRIGGER_STORAGE_CACHE, (obrpc::ObTriggerStorageCacheArg));
  #endif
  RPC_S(PR5 remote_write_ddl_inc_commit_log, OB_REMOTE_WRITE_DDL_INC_COMMIT_LOG, (obrpc::ObRpcRemoteWriteDDLIncCommitLogArg), ObRpcRemoteWriteDDLIncCommitLogRes);
  RPC_S(PR5 clean_sequence_cache, obrpc::OB_CLEAN_SEQUENCE_CACHE, (obrpc::UInt64), obrpc::ObSeqCleanCacheRes);
  RPC_S(PR5 register_tx_data, OB_REGISTER_TX_DATA, (ObRegisterTxDataArg), ObRegisterTxDataResult);
  RPC_S(PR5 query_ls_is_valid_member, OB_QUERY_LS_IS_VALID_MEMBER, (ObQueryLSIsValidMemberRequest),
      ObQueryLSIsValidMemberResponse);
  RPC_S(PR5 check_backup_dest_connectivity, OB_CHECK_BACKUP_DEST_CONNECTIVITY, (ObCheckBackupConnectivityArg));
  RPC_S(PR5 estimate_tablet_block_count, OB_ESTIMATE_TABLET_BLOCK_COUNT, (ObEstBlockArg), ObEstBlockRes);
  RPC_S(PR5 gen_unique_id, OB_GEN_UNIQUE_ID, (obrpc::UInt64), share::ObCommonID);
  RPC_AP(PR1 get_ls_sync_scn, OB_GET_LS_SYNC_SCN, (obrpc::ObGetLSSyncScnArg), obrpc::ObGetLSSyncScnRes);
  RPC_S(PR5 sync_rewrite_rules, OB_SYNC_REWRITE_RULES, (ObSyncRewriteRuleArg));
  RPC_S(PR5 force_set_ls_as_single_replica, OB_LOG_FORCE_SET_LS_AS_SINGLE_REPLICA, (obrpc::ObForceSetLSAsSingleReplicaArg));
  RPC_AP(PR5 net_endpoint_predict_ingress, OB_PREDICT_INGRESS_BW, (obrpc::ObNetEndpointPredictIngressArg), obrpc::ObNetEndpointPredictIngressRes);
  RPC_AP(PR5 net_endpoint_set_ingress, OB_SET_INGRESS_BW, (obrpc::ObNetEndpointSetIngressArg), obrpc::ObNetEndpointSetIngressRes);
  RPC_S(PR5 session_info_verification, OB_SESS_INFO_VERIFICATION, (ObSessInfoVerifyArg), ObSessionInfoVeriRes);
  RPC_S(PR5 detect_session_alive, OB_DETECT_SESSION_ALIVE, (obrpc::UInt64), obrpc::Bool);
  RPC_AP(PR5 broadcast_consensus_version, OB_BROADCAST_CONSENSUS_VERSION, (obrpc::ObBroadcastConsensusVersionArg), obrpc::ObBroadcastConsensusVersionRes);
  RPC_S(PR5 direct_load_control, OB_DIRECT_LOAD_CONTROL, (observer::ObDirectLoadControlRequest), observer::ObDirectLoadControlResult);
  RPC_S(PR5 direct_load_resource, OB_DIRECT_LOAD_RESOURCE, (observer::ObDirectLoadResourceOpRequest), observer::ObDirectLoadResourceOpResult);
  RPC_S(PR5 dispatch_ttl, OB_TABLE_TTL, (obrpc::ObTTLRequestArg), obrpc::ObTTLResponseArg);
  RPC_AP(PR5 flush_ls_archive, OB_FLUSH_LS_ARCHIVE, (obrpc::ObFlushLSArchiveArg), obrpc::Int64);
  RPC_AP(PR5 tablet_major_freeze, OB_TABLET_MAJOR_FREEZE, (ObTabletMajorFreezeArg), obrpc::Int64);
  RPC_AP(PR5 kill_client_session, OB_KILL_CLIENT_SESSION, (ObKillClientSessionArg), ObKillClientSessionRes);
  RPC_S(PR5 client_session_create_time, OB_CLIENT_SESSION_CONNECT_TIME, (ObClientSessionCreateTimeAndAuthArg), ObClientSessionCreateTimeAndAuthRes);
  RPC_S(PR5 cancel_gather_stats, OB_CANCEL_GATHER_STATS, (ObCancelGatherStatsArg));
  RPC_S(PR5 force_set_tenant_log_disk, OB_LOG_FORCE_SET_TENANT_LOG_DISK, (obrpc::ObForceSetTenantLogDiskArg));
  RPC_S(PR5 dump_server_usage, OB_FORCE_DUMP_SERVER_USAGE, (obrpc::ObDumpServerUsageRequest), obrpc::ObDumpServerUsageResult);
  RPC_S(PR5 change_external_storage_dest, obrpc::OB_CHANGE_EXTERNAL_STORAGE_DEST, (ObAdminSetConfigArg));
  RPC_S(PR5 phy_res_calculate_by_unit, OB_CAL_UNIT_PHY_RESOURCE, (obrpc::Int64), share::ObMinPhyResourceResult);
  RPC_S(PR5 rpc_reverse_keepalive, OB_RPC_REVERSE_KEEPALIVE, (obrpc::ObRpcReverseKeepaliveArg), obrpc::ObRpcReverseKeepaliveResp);
  RPC_AP(PR5 kill_query_client_session, OB_KILL_QUERY_CLIENT_SESSION, (ObKillQueryClientSessionArg), obrpc::Int64);
  RPC_S(PR5 fetch_stable_member_list, OB_FETCH_STABLE_MEMBER_LIST, (obrpc::ObFetchStableMemberListArg), obrpc::ObFetchStableMemberListInfo);
  RPC_S(PR5 broadcast_config_version, OB_BROADCAST_CONFIG_VERSION, (ObBroadcastConfigVersionArg));
  RPC_S(PR5 notify_start_archive, OB_NOTIFY_START_ARCHIVE, (obrpc::ObNotifyStartArchiveArg));
  RPC_S(PR5 estimate_skip_rate, OB_ESTIMATE_SKIP_RATE, (obrpc::ObEstSkipRateArg), ObEstSkipRateRes);
  RPC_S(PR5 check_nested_mview_mds, OB_CHECK_NESTED_MVIEW_MDS, (obrpc::ObCheckNestedMViewMdsArg), obrpc::ObCheckNestedMViewMdsRes);
  RPC_AP(PRZ load_tenant_table_schema, OB_LOAD_TENANT_TABLE_SCHEMA, (obrpc::ObLoadTenantTableSchemaArg));
  RPC_S(PR5 force_drop_lonely_lob_aux_table, OB_ADMIN_FORCE_DROP_LONELY_LOB_AUX_TABLE, (obrpc::ObForceDropLonelyLobAuxTableArg));
