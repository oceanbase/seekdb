ob_set_subtarget(ob_rootserver ALONE
  ob_ddl_service.cpp
  ob_ddl_service_launcher.cpp
  ob_root_service.cpp
  ddl_task/ob_ddl_redefinition_task.cpp
  ob_lonely_table_cleaner.cpp
)

ob_set_subtarget(ob_rootserver backup
  backup/ob_archive_scheduler_service.cpp
  backup/ob_backup_base_service.cpp
  backup/ob_tenant_archive_scheduler.cpp 
  backup/ob_backup_proxy.cpp
  backup/ob_backup_param_operator.cpp 
)

ob_set_subtarget(ob_rootserver common
  ob_bootstrap.cpp
  ob_ddl_help.cpp
  ob_ddl_operator.cpp
  ob_ddl_sql_generator.cpp
  ob_index_builder.cpp
  ob_mlog_builder.cpp
  ob_partition_creator.cpp
  ob_root_minor_freeze.cpp
  ob_root_utils.cpp
  ob_root_inspection.cpp
  ob_rs_event_history_table_operator.cpp
  ob_tenant_event_history_table_operator.cpp
  ob_rs_job_table_operator.cpp
  ob_rs_reentrant_thread.cpp
  ob_schema2ddl_sql.cpp
  ob_snapshot_info_manager.cpp
  ob_tablet_creator.cpp
  ob_tablet_drop.cpp
  ob_table_creator.cpp
  ob_thread_idling.cpp
  ob_upgrade_executor.cpp
  ob_vertical_partition_builder.cpp
  ob_tenant_thread_helper.cpp
  ob_system_admin_util.cpp
  ob_schema_history_recycler.cpp
  ob_lob_piece_builder.cpp
  ob_lob_meta_builder.cpp
  ob_split_partition_helper.cpp
  ob_partition_exchange.cpp
  ob_catalog_ddl_operator.cpp
  ob_catalog_ddl_service.cpp
  ob_tenant_ddl_service.cpp
  ob_load_inner_table_schema_executor.cpp
  ob_alter_table_constraint_checker.cpp
  ob_ccl_ddl_service.cpp
  ob_ccl_ddl_operator.cpp
  ob_ai_model_ddl_operator.cpp
  ob_ai_model_ddl_service.cpp
  ob_location_ddl_operator.cpp
  ob_location_ddl_service.cpp
  ob_objpriv_mysql_ddl_operator.cpp
  ob_objpriv_mysql_ddl_service.cpp
  ob_objpriv_mysql_schema_history_recycler.cpp
  ob_create_index_on_empty_table_helper.cpp
)

ob_set_subtarget(ob_rootserver ddl_task
  ddl_task/ob_column_redefinition_task.cpp
  ddl_task/ob_constraint_task.cpp
  ddl_task/ob_ddl_retry_task.cpp
  ddl_task/ob_ddl_scheduler.cpp
  ddl_task/ob_ddl_single_replica_executor.cpp
  ddl_task/ob_ddl_task.cpp
  ddl_task/ob_drop_index_task.cpp
  ddl_task/ob_drop_fts_index_task.cpp
  ddl_task/ob_drop_vec_index_task.cpp
  ddl_task/ob_drop_primary_key_task.cpp
  ddl_task/ob_index_build_task.cpp
  ddl_task/ob_build_mview_task.cpp
  ddl_task/ob_fts_index_build_task.cpp
  ddl_task/ob_modify_autoinc_task.cpp
  ddl_task/ob_table_redefinition_task.cpp
  ddl_task/ob_partition_split_task.cpp
  ddl_task/ob_recover_restore_table_task.cpp
  ddl_task/ob_ddl_tablet_scheduler.cpp
  ddl_task/ob_drop_vec_ivf_index_task.cpp
  ddl_task/ob_rebuild_index_task.cpp
  ddl_task/ob_vec_index_build_task.cpp
  fork_table/ob_fork_database_service.cpp
  ddl_task/ob_sys_ddl_util.cpp
  ddl_task/ob_vec_ivf_index_build_task.cpp
  ddl_task/ob_drop_lob_task.cpp
)

ob_set_subtarget(ob_rootserver parallel_ddl
  parallel_ddl/ob_ddl_helper.cpp
  parallel_ddl/ob_create_table_helper.cpp
  parallel_ddl/ob_create_view_helper.cpp
  parallel_ddl/ob_create_materialized_view_helper.cpp
  parallel_ddl/ob_index_name_checker.cpp
  parallel_ddl/ob_set_comment_helper.cpp
  parallel_ddl/ob_create_index_helper.cpp
  parallel_ddl/ob_update_index_status_helper.cpp
  parallel_ddl/ob_drop_table_helper.cpp
  parallel_ddl/ob_htable_ddl_handler.cpp
  parallel_ddl/ob_set_kv_attribute_helper.cpp
  parallel_ddl/ob_create_tablegroup_helper.cpp
  parallel_ddl/ob_drop_tablegroup_helper.cpp
  parallel_ddl/ob_table_helper.cpp
  parallel_ddl/ob_create_table_like_helper.cpp
)

ob_set_subtarget(ob_rootserver freeze
  freeze/ob_tenant_major_freeze.cpp
  freeze/ob_major_merge_scheduler.cpp
  freeze/ob_major_merge_progress_checker.cpp
  freeze/ob_freeze_info_detector.cpp
  freeze/ob_freeze_reentrant_thread.cpp
  freeze/ob_tenant_all_zone_merge_strategy.cpp
  freeze/ob_tenant_major_merge_strategy.cpp
  freeze/ob_zone_merge_manager.cpp
  freeze/ob_major_merge_info_manager.cpp
  freeze/ob_daily_major_freeze_launcher.cpp
  freeze/ob_checksum_validator.cpp
  freeze/ob_major_freeze_service.cpp
  freeze/ob_major_freeze_rpc_define.cpp
  freeze/ob_major_freeze_helper.cpp
  freeze/ob_major_freeze_util.cpp
  freeze/ob_major_merge_progress_util.cpp
  freeze/ob_fts_checksum_validate_util.cpp
)

ob_set_subtarget(ob_rootserver virtual_table
  virtual_table/ob_virtual_core_inner_table.cpp
)

ob_set_subtarget(ob_rootserver mview
  mview/ob_mview_maintenance_service.cpp
  mview/ob_mlog_maintenance_task.cpp
  mview/ob_mview_maintenance_task.cpp
  mview/ob_mview_refresh_stats_maintenance_task.cpp
  mview/ob_mview_timer_task.cpp
  mview/ob_mview_dependency_service.cpp
  mview/ob_mview_push_refresh_scn_task.cpp
  mview/ob_mview_push_snapshot_task.cpp
  mview/ob_replica_safe_check_task.cpp
  mview/ob_mview_clean_snapshot_task.cpp
  mview/ob_mview_update_cache_task.cpp
  mview/ob_mview_mds_op_task.cpp
  mview/ob_mview_alter_service.cpp
  mview/ob_mview_update_deps_task.cpp
)

ob_set_subtarget(ob_rootserver direct_load
  direct_load/ob_direct_load_partition_exchange.cpp
)

ob_set_subtarget(ob_rootserver fork_table
  fork_table/ob_fork_table_info_builder.cpp
  fork_table/ob_fork_table_helper.cpp
  fork_table/ob_fork_table_service.cpp
  fork_table/ob_fork_table_task.cpp
)

ob_set_subtarget(ob_rootserver standby
  standby/ob_service_grpc.cpp
  standby/ob_standby_service.cpp
  standby/ob_tenant_role_transition_service.cpp
)

ob_set_subtarget(ob_rootserver truncate_info
  truncate_info/ob_truncate_info_service.cpp
)

ob_set_subtarget(ob_rootserver pl_ddl
  pl_ddl/ob_pl_ddl_service.cpp
  pl_ddl/ob_pl_ddl_operator.cpp
)

ob_server_add_target(ob_rootserver)
