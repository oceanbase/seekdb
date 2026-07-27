ob_set_subtarget(ob_rootserver ALONE
  ob_debug_sync_rs.cpp
  ob_ddl_service.cpp
  ob_ddl_service_launcher.cpp
  ob_local_management_service.cpp
  ddl_task/ob_ddl_redefinition_task.cpp
  ob_lonely_table_cleaner.cpp
)

ob_set_subtarget(ob_rootserver common
  ddl_task/ob_ddl_common_rs_impl.cpp
  ob_snapshot_table_proxy.cpp
  ddl_task/ob_ddl_longops.cpp
  ob_bootstrap.cpp
  ob_ddl_operator.cpp
  ob_admin_job_table_operator.cpp
  ob_dependency_ddl_helper.cpp
  ob_ddl_sql_generator.cpp
  ob_index_builder.cpp
  ob_partition_creator.cpp
  ob_root_minor_freeze.cpp
  ob_root_utils.cpp
  ob_root_inspection.cpp
  ob_rs_reentrant_thread.cpp
  ob_schema2ddl_sql.cpp
  ob_snapshot_info_manager.cpp
  ob_tablet_creator.cpp
  ob_tablet_drop.cpp
  ob_table_creator.cpp
  ob_thread_idling.cpp
  ob_vertical_partition_builder.cpp
  ob_server_thread_helper.cpp
  ob_system_admin_util.cpp
  ob_lob_piece_builder.cpp
  ob_lob_meta_builder.cpp
  ob_partition_exchange.cpp
  ob_runtime_ddl_service.cpp
  ob_load_inner_table_schema_executor.cpp
  ob_alter_table_constraint_checker.cpp
  ob_ai_model_ddl_operator.cpp
  ob_ai_model_ddl_service.cpp
  ob_objpriv_mysql_ddl_operator.cpp
  ob_objpriv_mysql_ddl_service.cpp
  ob_create_index_on_empty_table_helper.cpp
)

ob_set_subtarget(ob_rootserver ddl_task
  ddl_task/ob_column_redefinition_task.cpp
  ddl_task/ob_constraint_task.cpp
  ddl_task/ob_ddl_retry_task.cpp
  ddl_task/ob_ddl_scheduler.cpp
  ddl_task/ob_ddl_local_build_executor.cpp
  ddl_task/ob_ddl_task.cpp
  ddl_task/ob_drop_index_task.cpp
  ddl_task/ob_drop_fts_index_task.cpp
  ddl_task/ob_drop_vec_index_task.cpp
  ddl_task/ob_drop_primary_key_task.cpp
  ddl_task/ob_index_build_task.cpp
  ddl_task/ob_fts_index_build_task.cpp
  ddl_task/ob_modify_autoinc_task.cpp
  ddl_task/ob_table_redefinition_task.cpp
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
  parallel_ddl/ob_set_comment_helper.cpp
  parallel_ddl/ob_create_index_helper.cpp
  parallel_ddl/ob_update_index_status_helper.cpp
  parallel_ddl/ob_drop_table_helper.cpp
  parallel_ddl/ob_table_helper.cpp
  parallel_ddl/ob_create_table_like_helper.cpp
)

ob_set_subtarget(ob_rootserver freeze
  freeze/ob_local_major_freeze.cpp
  freeze/ob_global_merge_manager.cpp
  freeze/ob_major_merge_scheduler.cpp
  freeze/ob_major_merge_progress_checker.cpp
  freeze/ob_freeze_info_detector.cpp
  freeze/ob_snapshot_gc_scn_renewer.cpp
  freeze/ob_freeze_reentrant_thread.cpp
  freeze/ob_major_merge_info_manager.cpp
  freeze/ob_daily_major_freeze_launcher.cpp
  freeze/ob_checksum_validator.cpp
  freeze/ob_major_freeze_service.cpp
  freeze/ob_major_freeze_helper.cpp
  freeze/ob_major_freeze_util.cpp
  freeze/ob_major_merge_progress_util.cpp
  freeze/ob_fts_checksum_validate_util.cpp
)

ob_set_subtarget(ob_rootserver virtual_table
  virtual_table/ob_virtual_core_inner_table.cpp
)


ob_set_subtarget(ob_rootserver fork_table
  fork_table/ob_fork_table_info_builder.cpp
  fork_table/ob_fork_table_helper.cpp
  fork_table/ob_fork_table_util.cpp
  ob_domain_index_builder_util.cpp
  fork_table/ob_fork_table_service.cpp
  fork_table/ob_fork_table_task.cpp
)


ob_set_subtarget(ob_rootserver truncate_info
  truncate_info/ob_truncate_info_service.cpp
)

ob_set_subtarget(ob_rootserver pl_ddl
  pl_ddl/ob_pl_ddl_service.cpp
  pl_ddl/ob_pl_ddl_operator.cpp
)

ob_server_add_target(ob_rootserver)
