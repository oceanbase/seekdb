storage_unittest(test_session_track_sys_var mysql/test_session_track_sys_var.cpp)
ob_unittest(test_uniq_task_queue)

if (SEEKDB_ENABLE_EXPERIMENTAL_PLUGINS)
  ob_unittest(test_server_plugin_runtime)
  target_link_libraries(test_server_plugin_runtime PRIVATE seekdb_plugin_sdk)
endif()
