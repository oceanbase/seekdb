function(schema_unittest case)
  ob_unittest(${ARGV})
  target_sources(${case}
    PRIVATE
    mock_schema_service.h
    schema_test_utils.h)
  target_link_libraries(${case} PRIVATE mockcontainer)
endfunction()

schema_unittest(test_table_dml_param)
schema_unittest(test_table_schema)
