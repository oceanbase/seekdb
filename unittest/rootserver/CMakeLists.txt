function(rs_unittest case)
  ob_unittest(${ARGV})
  target_sources(${case} PRIVATE 
  ob_rs_test_utils.h 
  ob_rs_test_utils.cpp 
  )
  target_link_libraries(${case} PRIVATE mockcontainer)
endfunction()


#rs_unittest(test_bootstrap)
rs_unittest(test_rs_reentrant_thread)
#rs_unittest(test_multi_cluster_manager)
#rs_unittest(test_ob_backup_task_scheduler)
rs_unittest(test_ddl_task_struct)
rs_unittest(test_parallel_ddl_control)
rs_unittest(test_max_id_cache_unittest)
