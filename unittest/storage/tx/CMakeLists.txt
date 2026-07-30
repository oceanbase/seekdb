add_library(mock_tx_log_adapter SHARED
  ob_mock_tx_log_adapter.cpp ob_mock_tx_log_adapter.h)

target_link_libraries(mock_tx_log_adapter PUBLIC oceanbase mockcontainer)
function(tx_unittest case)
  ob_unittest(${ARGV})
  target_link_libraries(${case} PUBLIC mock_tx_log_adapter)
endfunction()

# tx_unittest(test_simple_tx_ctx)
tx_unittest(test_ob_trans_hashmap)

storage_unittest(test_ob_tx_log)
storage_unittest(test_ob_timestamp_service)
storage_unittest(test_undo_action)
storage_unittest(test_ob_id_meta)
# storage_unittest(test_ob_standby_read)
storage_unittest(test_ob_trans_tlog)
storage_unittest(test_tx_timestamp_waiter)
storage_unittest(test_ob_time_wheel)
add_subdirectory(it)

storage_unittest(test_redo_submitter)
storage_unittest(test_trans_callback_mgr_fill_redo)
storage_unittest(test_misc)
