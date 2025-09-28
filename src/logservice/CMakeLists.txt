ob_set_subtarget(ob_logservice common
  ob_append_callback.cpp
  ob_location_adapter.cpp
  ob_log_base_header.cpp
  ob_log_handler.cpp
  ob_log_handler_base.cpp
  ob_log_service.cpp
  ob_ls_adapter.cpp
  ob_reporter_adapter.cpp
  ob_switch_leader_adapter.cpp
  ob_server_log_block_mgr.cpp
  ob_log_flashback_service.cpp
  ob_net_keepalive_adapter.cpp
  ob_log_external_storage_handler.cpp
  ob_log_external_storage_utils.cpp
  ob_log_monitor.cpp
  ob_locality_adapter.cpp
  ob_reconfig_checker_adapter.cpp
)

ob_set_subtarget(ob_logservice common_mixed
  applyservice/ob_log_apply_service.cpp
  logrpc/ob_log_request_handler.cpp
  logrpc/ob_log_rpc_req.cpp
  rcservice/ob_role_change_handler.cpp
  rcservice/ob_role_change_service.cpp
  replayservice/ob_log_replay_service.cpp
  replayservice/ob_replay_handler.cpp
  replayservice/ob_replay_status.cpp
  replayservice/ob_tablet_replay_executor.cpp
)

ob_set_subtarget(ob_logservice leader_coordinator
  leader_coordinator/ob_failure_detector.cpp
  leader_coordinator/ob_leader_coordinator.cpp
  leader_coordinator/table_accessor.cpp
  leader_coordinator/election_priority_impl/election_priority_impl.cpp
  leader_coordinator/election_priority_impl/election_priority_v1.cpp
)

ob_set_subtarget(ob_logservice palf
  palf/block_gc_timer_task.cpp
  palf/fetch_log_engine.cpp
  palf/log_block_handler.cpp
  palf/log_block_header.cpp
  palf/log_block_mgr.cpp
  palf/log_checksum.cpp
  palf/log_config_mgr.cpp
  palf/log_define.cpp
  palf/log_engine.cpp
  palf/log_entry.cpp
  palf/log_entry_header.cpp
  palf/log_group_buffer.cpp
  palf/log_group_entry.cpp
  palf/log_group_entry_header.cpp
  palf/log_shared_queue_thread.cpp
  palf/log_io_task.cpp
  palf/log_io_task_cb_thread_pool.cpp
  palf/log_io_task_cb_utils.cpp
  palf/log_shared_task.cpp
  palf/log_io_worker.cpp
  palf/log_iterator_storage.cpp
  palf/log_learner.cpp
  palf/log_loop_thread.cpp
  palf/log_meta.cpp
  palf/log_meta_entry.cpp
  palf/log_meta_entry_header.cpp
  palf/log_meta_info.cpp
  palf/log_net_service.cpp
  palf/log_reader.cpp
  palf/log_reader_utils.cpp
  palf/log_reconfirm.cpp
  palf/log_req.cpp
  palf/log_request_handler.cpp
  palf/log_rpc.cpp
  palf/log_rpc_processor.cpp
  palf/log_rpc_proxy.cpp
  palf/log_simple_member_list.cpp
  palf/log_sliding_window.cpp
  palf/log_state_mgr.cpp
  palf/log_storage.cpp
  palf/log_task.cpp
  palf/log_writer_utils.cpp
  palf/lsn.cpp
  palf/lsn_allocator.cpp
  palf/palf_base_info.cpp
  palf/palf_callback_wrapper.cpp
  palf/palf_env.cpp
  palf/palf_env_impl.cpp
  palf/palf_handle.cpp
  palf/palf_handle_impl.cpp
  palf/log_cache.cpp
  palf/palf_handle_impl_guard.cpp
  palf/log_block_pool_interface.cpp
  palf/palf_options.cpp
  palf/log_mode_mgr.cpp
  palf/log_updater.cpp
  palf/log_io_utils.cpp
  palf/log_io_worker_wrapper.cpp
  palf/log_throttle.cpp
  palf/log_io_context.cpp
  palf/log_io_adapter.cpp
)

ob_set_subtarget(ob_logservice palf_election
  palf/election/algorithm/election_impl.cpp
  palf/election/message/election_message.cpp
  palf/election/utils/election_event_recorder.cpp
  palf/election/utils/election_member_list.cpp
  palf/election/utils/election_utils.cpp
)

ob_set_subtarget(ob_logservice common_util
  common_util/ob_log_time_utils.cpp
  common_util/ob_log_ls_define.cpp
)

ob_server_add_target(ob_logservice)
