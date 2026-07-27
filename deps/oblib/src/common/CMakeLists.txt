ob_set_subtarget(oblib_common common
  ob_obj_deep_copy.cpp
  ob_hex_utils_base.cpp
  ob_accuracy.cpp
  ob_field.cpp
  ob_file_common_header.cpp
  ob_range.cpp
  ob_record_header.cpp
  ob_role.cpp
  ob_store_format.cpp
  ob_store_range.cpp
  ob_string_buf.ipp
  ob_tablet_id.cpp
  ob_timeout_ctx.cpp
  ob_version_def.cpp
  ob_data_version_mgr.cpp
)

ob_set_subtarget(oblib_common common_mixed
  cell/ob_cell_reader.cpp
  cell/ob_cell_writer.cpp
  object/ob_obj_compare.cpp
  object/ob_obj_type.cpp
  object/ob_object.cpp
  row/ob_row.cpp
  row/ob_row_checksum.cpp
  row/ob_row_desc.cpp
  row/ob_row_store.cpp
  row/ob_row_util.cpp
  rowkey/ob_rowkey.cpp
  rowkey/ob_rowkey_info.cpp
  rowkey/ob_store_rowkey.cpp
  sql_mode/ob_sql_mode_utils.cpp
)

ob_set_subtarget(oblib_common log
  log/ob_log_cursor.cpp
  log/ob_log_data_writer.cpp
  log/ob_log_dir_scanner.cpp
  log/ob_log_entry.cpp
  log/ob_log_generator.cpp
  log/ob_log_reader.cpp
  log/ob_single_log_reader.cpp
)

ob_set_subtarget(oblib_common storage
  storage/ob_freeze_define.cpp
  storage/ob_sequence.cpp
)



ob_set_subtarget(oblib_common reloc_enumset
  enumset/ob_enum_set_meta.cpp
)

ob_set_subtarget(oblib_common reloc_wide_integer
  wide_integer/ob_wide_integer.cpp
  wide_integer/ob_wide_integer_cmp_funcs.cpp
  wide_integer/ob_wide_integer_str_funcs.cpp
)

ob_set_subtarget(oblib_common reloc_udt
  udt/ob_udt_type.cpp
  udt/ob_collection_type.cpp
  udt/ob_array_type.cpp
  udt/ob_array_binary.cpp
  udt/ob_array_nested.cpp
  udt/ob_array_utils.cpp
  udt/ob_vector_type.cpp
  udt/ob_map_type.cpp
)

ob_set_subtarget(oblib_common reloc_json_type
  json_type/ob_json_path.cpp
  json_type/ob_json_tree.cpp
  json_type/ob_json_bin.cpp
  json_type/ob_json_base.cpp
  json_type/ob_json_parse.cpp
  json_type/ob_json_schema.cpp
  json_type/ob_json_diff.cpp
)

ob_set_subtarget(oblib_common reloc_number
  number/ob_number_v2.cpp
)

ob_set_subtarget(oblib_common reloc_lob
  lob/ob_lob_base.cpp
)

ob_set_subtarget(oblib_common reloc_xml
  xml/ob_mul_mode_reader.cpp
  xml/ob_xml_parser.cpp
  xml/ob_libxml2_sax_handler.cpp
  xml/ob_tree_base.cpp
  xml/ob_xml_tree.cpp
  xml/ob_xml_util.cpp
  xml/ob_xpath.cpp
  xml/ob_multi_mode_bin.cpp
  xml/ob_multi_mode_interface.cpp
  xml/ob_path_parser.cpp
  xml/ob_binary_aggregate.cpp
)

ob_set_subtarget(oblib_common reloc_datum
  datum/ob_datum.cpp
)

ob_set_subtarget(oblib_common reloc_timezone
  timezone/ob_datetime_format_models.cpp
  timezone/ob_time_convert.cpp
  timezone/ob_timezone_info.cpp
)

ob_set_subtarget(oblib_common ALONE
  timezone/ob_timezone_util.cpp
)

ob_set_subtarget(oblib_common reloc_mysqlclient
  mysqlclient/ob_isql_client.cpp
  mysqlclient/ob_mysql_proxy.cpp
  mysqlclient/ob_mysql_proxy_util.cpp
  mysqlclient/ob_mysql_result.cpp
  mysqlclient/ob_mysql_transaction.cpp
  mysqlclient/ob_single_connection_proxy.cpp
)

ob_set_subtarget(oblib_common reloc_geo
)

ob_set_subtarget(oblib_common ALONE
)

ob_lib_add_target(oblib_common)
