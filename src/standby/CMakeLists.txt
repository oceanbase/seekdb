if(OB_ENABLE_STANDBY)
  ob_set_subtarget(ob_standby_runtime common
    ob_standby_source_util.cpp
    ob_standby_schema_refresh_trigger.cpp
    ob_standby_grpc.cpp
    ob_standby_grpc_stream_util.cpp
    ob_standby_bootstrap_service.cpp
    ob_standby_log_sync_service.cpp
    ob_standby_palf_base_info.cpp
    control/ob_standby_role_transition_service.cpp
    control/ob_standby_timestamp_provider.cpp
    control/standby_state_store.cpp
    restore/ob_physical_copy_ctx.cpp
    restore/ob_physical_copy_task.cpp
    restore/ob_restore_helper.cpp
    restore/ob_restore_helper_ctx.cpp
    restore/ob_sstable_copy_finish_task.cpp
    restore/ob_standby_restore_macro_block_writer.cpp
    restore/ob_standby_restore_reader.cpp
    restore/ob_standby_restore_rpc.cpp
    restore/ob_standby_restore_tablet_builder.cpp
    restore/ob_standby_sstable_copier.cpp
    restore/ob_storage_restore_struct.cpp
    restore/ob_tablet_copy_finish_task.cpp
  )
  ob_set_subtarget(ob_standby common standby_module.cpp)
  ob_server_add_target(ob_standby_runtime)
  if(WIN32)
    # ob_standby_runtime compiles ob_standby_grpc.cpp which includes the
    # protoc-regenerated grpc headers; wait for oblib_grpc so the build-tree
    # standbyservice.pb.* exist before this object target is compiled.
    add_dependencies(ob_standby_runtime oblib_grpc)
  endif()
else()
  ob_set_subtarget(ob_standby common
    standby_module_disabled.cpp
    control/standby_state_store.cpp)
endif()

ob_server_add_target(ob_standby)
