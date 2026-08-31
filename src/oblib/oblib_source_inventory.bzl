"""Ordered source inventory owned by the native OBLib Bazel modules."""

OBLIB_UNITY_GROUPS = {
    "src/oblib/common:oblib_common": [
        struct(
            name = "oblib_common_common_0",
            language = "c++",
            srcs = [
                "src/oblib/common/ob_obj_deep_copy.cpp",
                "src/oblib/common/ob_hex_utils_base.cpp",
                "src/oblib/common/ob_accuracy.cpp",
                "src/oblib/common/ob_field.cpp",
                "src/oblib/common/ob_file_common_header.cpp",
                "src/oblib/common/ob_range.cpp",
                "src/oblib/common/ob_record_header.cpp",
                "src/oblib/common/ob_role.cpp",
                "src/oblib/common/ob_store_format.cpp",
                "src/oblib/common/ob_store_range.cpp",
                "src/oblib/common/ob_tablet_id.cpp",
                "src/oblib/common/ob_timeout_ctx.cpp",
                "src/oblib/common/ob_version_def.cpp",
                "src/oblib/common/ob_data_version_mgr.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_common_mixed_0",
            language = "c++",
            srcs = [
                "src/oblib/common/cell/ob_cell_reader.cpp",
                "src/oblib/common/cell/ob_cell_writer.cpp",
                "src/oblib/common/object/ob_obj_compare.cpp",
                "src/oblib/common/object/ob_obj_type.cpp",
                "src/oblib/common/object/ob_object.cpp",
                "src/oblib/common/row/ob_row.cpp",
                "src/oblib/common/row/ob_row_checksum.cpp",
                "src/oblib/common/row/ob_row_desc.cpp",
                "src/oblib/common/row/ob_row_store.cpp",
                "src/oblib/common/row/ob_row_util.cpp",
                "src/oblib/common/rowkey/ob_rowkey.cpp",
                "src/oblib/common/rowkey/ob_rowkey_info.cpp",
                "src/oblib/common/rowkey/ob_store_rowkey.cpp",
                "src/oblib/common/sql_mode/ob_sql_mode_utils.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_log_0",
            language = "c++",
            srcs = [
                "src/oblib/common/log/ob_log_cursor.cpp",
                "src/oblib/common/log/ob_log_data_writer.cpp",
                "src/oblib/common/log/ob_log_dir_scanner.cpp",
                "src/oblib/common/log/ob_log_entry.cpp",
                "src/oblib/common/log/ob_log_generator.cpp",
                "src/oblib/common/log/ob_log_reader.cpp",
                "src/oblib/common/log/ob_single_log_reader.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_reloc_datum_0",
            language = "c++",
            srcs = [
                "src/oblib/common/datum/ob_datum.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_reloc_enumset_0",
            language = "c++",
            srcs = [
                "src/oblib/common/enumset/ob_enum_set_meta.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_reloc_json_type_0",
            language = "c++",
            srcs = [
                "src/oblib/common/json_type/ob_json_path.cpp",
                "src/oblib/common/json_type/ob_json_tree.cpp",
                "src/oblib/common/json_type/ob_json_bin.cpp",
                "src/oblib/common/json_type/ob_json_base.cpp",
                "src/oblib/common/json_type/ob_json_parse.cpp",
                "src/oblib/common/json_type/ob_json_schema.cpp",
                "src/oblib/common/json_type/ob_json_diff.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_reloc_lob_0",
            language = "c++",
            srcs = [
                "src/oblib/common/lob/ob_lob_base.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_reloc_mysqlclient_0",
            language = "c++",
            srcs = [
                "src/oblib/common/mysqlclient/ob_isql_client.cpp",
                "src/oblib/common/mysqlclient/ob_mysql_proxy.cpp",
                "src/oblib/common/mysqlclient/ob_mysql_result.cpp",
                "src/oblib/common/mysqlclient/ob_mysql_transaction.cpp",
                "src/oblib/common/mysqlclient/ob_single_connection_proxy.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_reloc_number_0",
            language = "c++",
            srcs = [
                "src/oblib/common/number/ob_number_v2.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_reloc_timezone_0",
            language = "c++",
            srcs = [
                "src/oblib/common/timezone/ob_time_convert.cpp",
                "src/oblib/common/timezone/ob_timezone_info.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_reloc_udt_0",
            language = "c++",
            srcs = [
                "src/oblib/common/udt/ob_udt_type.cpp",
                "src/oblib/common/udt/ob_collection_type.cpp",
                "src/oblib/common/udt/ob_array_type.cpp",
                "src/oblib/common/udt/ob_array_binary.cpp",
                "src/oblib/common/udt/ob_array_nested.cpp",
                "src/oblib/common/udt/ob_array_utils.cpp",
                "src/oblib/common/udt/ob_vector_type.cpp",
                "src/oblib/common/udt/ob_map_type.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_reloc_wide_integer_0",
            language = "c++",
            srcs = [
                "src/oblib/common/wide_integer/ob_wide_integer.cpp",
                "src/oblib/common/wide_integer/ob_wide_integer_cmp_funcs.cpp",
                "src/oblib/common/wide_integer/ob_wide_integer_str_funcs.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_reloc_xml_0",
            language = "c++",
            srcs = [
                "src/oblib/common/xml/ob_mul_mode_reader.cpp",
                "src/oblib/common/xml/ob_xml_parser.cpp",
                "src/oblib/common/xml/ob_libxml2_sax_handler.cpp",
                "src/oblib/common/xml/ob_tree_base.cpp",
                "src/oblib/common/xml/ob_xml_tree.cpp",
                "src/oblib/common/xml/ob_xml_util.cpp",
                "src/oblib/common/xml/ob_xpath.cpp",
                "src/oblib/common/xml/ob_multi_mode_bin.cpp",
                "src/oblib/common/xml/ob_multi_mode_interface.cpp",
                "src/oblib/common/xml/ob_path_parser.cpp",
                "src/oblib/common/xml/ob_binary_aggregate.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_common_storage_0",
            language = "c++",
            srcs = [
                "src/oblib/common/storage/ob_freeze_define.cpp",
                "src/oblib/common/storage/ob_sequence.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
    ],
    "src/oblib/lib:ob_malloc_object": [
        struct(
            name = "ob_malloc_object_list_common_alloc_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/alloc/abit_set.cpp",
                "src/oblib/lib/utility/alloc_assist.cpp",
                "src/oblib/lib/alloc/alloc_failed_reason.cpp",
                "src/oblib/lib/alloc/alloc_func.cpp",
                "src/oblib/lib/alloc/alloc_struct.cpp",
                "src/oblib/lib/alloc/block_set.cpp",
                "src/oblib/lib/alloc/memory_dump.cpp",
                "src/oblib/lib/alloc/ob_malloc_allocator.cpp",
                "src/oblib/lib/alloc/ob_malloc_callback.cpp",
                "src/oblib/lib/alloc/ob_malloc_sample_struct.cpp",
                "src/oblib/lib/alloc/ob_ctx_allocator.cpp",
                "src/oblib/lib/alloc/object_mgr.cpp",
                "src/oblib/lib/alloc/object_set.cpp",
                "src/oblib/lib/resource/achunk_mgr.cpp",
                "src/oblib/lib/resource/ob_resource_mgr.cpp",
                "src/oblib/lib/allocator/ob_allocator_v2.cpp",
                "src/oblib/lib/allocator/ob_block_alloc_mgr.cpp",
                "src/oblib/lib/allocator/ob_concurrent_fifo_allocator.cpp",
                "src/oblib/lib/alloc/ob_ctx_define.cpp",
                "src/oblib/lib/allocator/ob_delay_free_allocator.cpp",
                "src/oblib/lib/allocator/ob_fifo_allocator.cpp",
                "src/oblib/lib/allocator/ob_hazard_ref.cpp",
                "src/oblib/lib/allocator/ob_malloc.cpp",
                "src/oblib/lib/utility/ob_mod_define.cpp",
                "src/oblib/lib/allocator/ob_page_manager.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "ob_malloc_object_list_common_alloc_1",
            language = "c++",
            srcs = [
                "src/oblib/lib/allocator/ob_slice_alloc.cpp",
                "src/oblib/lib/allocator/ob_tc_malloc.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
    ],
    "src/oblib/lib:oblib_lib": [
        struct(
            name = "oblib_lib_charset_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/charset/ob_ctype_bin.cc",
                "src/oblib/lib/charset/ob_ctype.cc",
                "src/oblib/lib/charset/ob_ctype_mb.cc",
                "src/oblib/lib/charset/ob_ctype_simple.cc",
                "src/oblib/lib/charset/ob_ctype_uca.cc",
                "src/oblib/lib/charset/ob_ctype_utf8.cc",
                "src/oblib/lib/charset/ob_dtoa.cc",
                "src/oblib/lib/charset/ob_charset.cpp",
                "src/oblib/lib/charset/uca900_ja_tbls.cc",
                "src/oblib/lib/charset/uca900_zh_tbls.cc",
                "src/oblib/lib/charset/uca900_zh2_tbls.cc",
                "src/oblib/lib/charset/uca900_zh3_tbls.cc",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_codec_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/codec/ob_fast_bp_func.cpp",
                "src/oblib/lib/codec/ob_generated_scalar_bp_func.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_common_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/ob_abort.cpp",
                "src/oblib/lib/ob_date_unit_type.cpp",
                "src/oblib/lib/ob_define.cpp",
                "src/oblib/lib/ob_lib_config.cpp",
                "src/oblib/lib/ob_name_id_def.cpp",
                "src/oblib/lib/ob_running_mode.cpp",
                "src/oblib/lib/runtime.cpp",
                "src/oblib/lib/worker.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_common_mixed_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/encode/ob_base64_encode.cpp",
                "src/oblib/lib/encode/ob_quoted_printable_encode.cpp",
                "src/oblib/lib/encode/ob_uuencode.cpp",
                "src/oblib/lib/atomic/ob_atomic_reference.cpp",
                "src/oblib/lib/checksum/ob_crc64.cpp",
                "src/oblib/lib/checksum/ob_parity_check.cpp",
                "src/oblib/lib/cpu/ob_cpu_topology.cpp",
                "src/oblib/lib/encrypt/ob_encrypted_helper.cpp",
                "src/oblib/lib/file/file_directory_utils.cpp",
                "src/oblib/lib/file/ob_file.cpp",
                "src/oblib/lib/file/ob_string_util.cpp",
                "src/oblib/lib/hash/ob_link_hashmap.cpp",
                "src/oblib/lib/hash/ob_linear_hash_map.cpp",
                "src/oblib/lib/hash/ob_dchash.cpp",
                "src/oblib/lib/hash/ob_hashutils.cpp",
                "src/oblib/lib/hash_func/murmur_hash.cpp",
                "src/oblib/lib/json/ob_json.cpp",
                "src/oblib/lib/json/ob_json_print_utils.cpp",
                "src/oblib/lib/json/ob_yson.cpp",
                "src/oblib/lib/lds/ob_lds_define.cpp",
                "src/oblib/lib/net/ob_addr.cpp",
                "src/oblib/lib/net/ob_net_util.cpp",
                "src/oblib/lib/objectpool/ob_server_object_pool.cpp",
                "src/oblib/lib/profile/ob_trace_id.cpp",
                "src/oblib/lib/profile/ob_trace_id_adaptor.cpp",
                "src/oblib/lib/thread/ob_dedup_queue.cpp",
                "src/oblib/lib/queue/ob_lighty_queue.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_common_mixed_0",
            language = "c",
            srcs = [
                "src/oblib/lib/hash_func/xxhash.c",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_common_mixed_1",
            language = "c++",
            srcs = [
                "src/oblib/lib/queue/ob_link_queue.cpp",
                "src/oblib/lib/queue/ob_ms_queue.cpp",
                "src/oblib/lib/random/ob_random.cpp",
                "src/oblib/lib/rc/context.cpp",
                "src/oblib/lib/rc/ob_rc.cpp",
                "src/oblib/lib/stat/ob_di_tls.cpp",
                "src/oblib/lib/stat/ob_diagnose_info.cpp",
                "src/oblib/lib/stat/ob_latch_define.cpp",
                "src/oblib/lib/stat/ob_stat_template.cpp",
                "src/oblib/lib/statistic_event/ob_stat_event.cpp",
                "src/oblib/lib/string/ob_sql_string.cpp",
                "src/oblib/lib/string/ob_string.cpp",
                "src/oblib/lib/string/ob_strings.cpp",
                "src/oblib/lib/string/ob_string_buffer.cpp",
                "src/oblib/lib/task/ob_timer.cpp",
                "src/oblib/lib/task/ob_timer_monitor.cpp",
                "src/oblib/lib/task/ob_timer_service.cpp",
                "src/oblib/lib/thread_local/ob_tsi_utils.cpp",
                "src/oblib/lib/thread_local/thread_buffer.cpp",
                "src/oblib/lib/time/Time.cpp",
                "src/oblib/lib/time/ob_cur_time.cpp",
                "src/oblib/lib/time/ob_time_utility.cpp",
                "src/oblib/lib/time/ob_tsc_timestamp.cpp",
                "src/oblib/lib/trace/ob_trace.cpp",
                "src/oblib/lib/trace/ob_trace_event.cpp",
                "src/oblib/lib/wait_event/ob_wait_class.cpp",
                "src/oblib/lib/wait_event/ob_wait_event.cpp",
                "src/oblib/lib/locale/ob_locale_type.cc",
                "src/oblib/lib/locale/ob_locale.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_downsink_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/time/ob_clock_generator.cpp",
                "src/oblib/lib/utility/ob_smart_call.cpp",
                "src/oblib/lib/utility/ob_smart_var.cpp",
                "src/oblib/lib/utility/ob_target_specific.cpp",
                "src/oblib/lib/utility/ob_common_utility.cpp",
                "src/oblib/lib/utility/data_buffer.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_downsink_qt_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/thread/ob_queue_thread.cpp",
                "src/oblib/lib/thread/ob_balance_filter.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_lock_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/lock/cond.cpp",
                "src/oblib/lib/lock/mutex.cpp",
                "src/oblib/lib/lock/ob_bucket_lock.cpp",
                "src/oblib/lib/lock/ob_latch.cpp",
                "src/oblib/lib/lock/ob_thread_cond.cpp",
                "src/oblib/lib/lock/ob_rwlock.cpp",
                "src/oblib/lib/lock/ob_futex.cpp",
                "src/oblib/lib/lock/ob_bucket_qsync_lock.cpp",
                "src/oblib/lib/lock/ob_qsync_lock.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_ob_vector_util_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/vector/ob_vector_util.cpp",
                "src/oblib/lib/vector/ob_vsag_adaptor.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_oblog_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/oblog/ob_async_log_struct.cpp",
                "src/oblib/lib/oblog/ob_base_log_buffer.cpp",
                "src/oblib/lib/oblog/ob_base_log_writer.cpp",
                "src/oblib/lib/oblog/ob_ringbuf_log_writer.cpp",
                "src/oblib/lib/oblog/ob_log.cpp",
                "src/oblib/lib/oblog/ob_log_compressor.cpp",
                "src/oblib/lib/oblog/ob_log_dba_event.cpp",
                "src/oblib/lib/oblog/ob_log_time_fmt.cpp",
                "src/oblib/lib/oblog/ob_trace_log.cpp",
                "src/oblib/lib/oblog/ob_warning_buffer.cpp",
                "src/oblib/lib/oblog/ob_syslog_rate_limiter.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_signal_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/signal/ob_signal_handlers.cpp",
                "src/oblib/lib/signal/ob_signal_struct.cpp",
                "src/oblib/lib/signal/ob_signal_utils.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_signal_0",
            language = "c",
            srcs = [
                "src/oblib/lib/signal/ob_libunwind.c",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_thread_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/thread/ob_async_task_queue.cpp",
                "src/oblib/lib/thread/ob_dynamic_thread_pool.cpp",
                "src/oblib/lib/thread/ob_reentrant_thread.cpp",
                "src/oblib/lib/thread/ob_map_queue_thread_pool.cpp",
                "src/oblib/lib/thread/protected_stack_allocator.cpp",
                "src/oblib/lib/thread/thread.cpp",
                "src/oblib/lib/thread/threads.cpp",
                "src/oblib/lib/thread/ob_thread_hook.cpp",
                "src/oblib/lib/thread/ob_pthread.cpp",
                "src/oblib/lib/thread/ob_thread_name.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_lib_utility_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/utility/ob_fast_convert.cpp",
                "src/oblib/lib/utility/ob_hang_fatal_error.cpp",
                "src/oblib/lib/utility/ob_platform_utils.cpp",
                "src/oblib/lib/utility/ob_print_utils.cpp",
                "src/oblib/lib/utility/ob_serialization_helper.cpp",
                "src/oblib/lib/utility/ob_tracepoint.cpp",
                "src/oblib/lib/utility/ob_utility.cpp",
                "src/oblib/lib/utility/utility.cpp",
                "src/oblib/lib/utility/ob_backtrace.cpp",
                "src/oblib/lib/utility/ob_hyperloglog.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
    ],
    "src/oblib/lib:oblib_lib_bitmap": [
        struct(
            name = "oblib_lib_bitmap_common_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/container/ob_bitmap.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
    ],
    "src/oblib/lib:oblib_lib_simd": [
        struct(
            name = "oblib_lib_simd_codec_simd_0",
            language = "c++",
            srcs = [
                "src/oblib/lib/codec/ob_fast_delta.cpp",
                "src/oblib/lib/codec/ob_generated_unalign_simd_bp_func.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
    ],
    "src/oblib/rpc:oblib_rpc": [
        struct(
            name = "oblib_rpc_common_0",
            language = "c++",
            srcs = [
                "src/oblib/rpc/ob_lock_wait_node.cpp",
                "src/oblib/rpc/ob_request.cpp",
                "src/oblib/rpc/ob_sql_request_operator.cpp",
                "src/oblib/rpc/ob_sql_mem_pool.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_rpc_frame_0",
            language = "c++",
            srcs = [
                "src/oblib/rpc/frame/ob_result_code.cpp",
                "src/oblib/rpc/frame/ob_net_easy.cpp",
                "src/oblib/rpc/frame/ob_req_deliver.cpp",
                "src/oblib/rpc/frame/ob_req_qhandler.cpp",
                "src/oblib/rpc/frame/ob_req_queue_thread.cpp",
                "src/oblib/rpc/frame/ob_req_translator.cpp",
                "src/oblib/rpc/frame/ob_sql_processor.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_rpc_obmysql_0",
            language = "c++",
            srcs = [
                "src/oblib/rpc/obmysql/ob_mysql_packet.cpp",
                "src/oblib/rpc/obmysql/ob_mysql_util.cpp",
                "src/oblib/rpc/obmysql/ob_nio_abi_check.cpp",
                "src/oblib/rpc/obmysql/ob_sql_nio_server.cpp",
                "src/oblib/rpc/obmysql/ob_sql_sock_handler.cpp",
                "src/oblib/rpc/obmysql/ob_sql_sock_session.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
        struct(
            name = "oblib_rpc_obmysql_packet_0",
            language = "c++",
            srcs = [
                "src/oblib/rpc/obmysql/packet/ompk_eof.cpp",
                "src/oblib/rpc/obmysql/packet/ompk_error.cpp",
                "src/oblib/rpc/obmysql/packet/ompk_local_infile.cpp",
                "src/oblib/rpc/obmysql/packet/ompk_ok.cpp",
                "src/oblib/rpc/obmysql/packet/ompk_auth_switch.cpp",
            ],
            generated_srcs = [
            ],
            external_srcs = [
            ],
        ),
    ],
}
OBLIB_STANDALONE_SOURCES = {
    "src/oblib/common:oblib_common": [
        struct(
            path = "src/oblib/common/timezone/ob_timezone_util.cpp",
            kind = "source",
            language = "c++",
        ),
    ],
    "src/oblib/lib/compress/zstd_1_3_8:zstd_1_3_8_objs": [
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/ob_zstd_wrapper.cpp",
            kind = "source",
            language = "c++",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/debug.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/entropy_common.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/error_private.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/fse_compress.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/fse_decompress.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/hist.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/huf_compress.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/huf_decompress.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/pool.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/threading.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/xxhash.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/zstd_common.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/zstd_compress.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/zstd_ddict.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/zstd_decompress.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/zstd_decompress_block.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/zstd_double_fast.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/zstd_fast.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/zstd_lazy.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/zstd_ldm.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/zstd_opt.c",
            kind = "source",
            language = "c",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/zstd_src/zstdmt_compress.c",
            kind = "source",
            language = "c",
        ),
    ],
    "src/oblib/lib/compress:compress": [
        struct(
            path = "src/oblib/lib/compress/none/ob_none_compressor.cpp",
            kind = "source",
            language = "c++",
        ),
        struct(
            path = "src/oblib/lib/compress/ob_compressor.cpp",
            kind = "source",
            language = "c++",
        ),
        struct(
            path = "src/oblib/lib/compress/ob_compressor_pool.cpp",
            kind = "source",
            language = "c++",
        ),
        struct(
            path = "src/oblib/lib/compress/zlib/ob_zlib_compressor.cpp",
            kind = "source",
            language = "c++",
        ),
        struct(
            path = "src/oblib/lib/compress/zstd_1_3_8/ob_zstd_compressor_1_3_8.cpp",
            kind = "source",
            language = "c++",
        ),
    ],
    "src/oblib/lib/restore:restore": [
        struct(
            path = "src/oblib/lib/restore/ob_io_device.cpp",
            kind = "source",
            language = "c++",
        ),
    ],
    "src/oblib/lib:malloc_hook": [
        struct(
            path = "src/oblib/lib/alloc/malloc_hook.cpp",
            kind = "source",
            language = "c++",
        ),
        struct(
            path = "src/oblib/lib/alloc/malloc_hook_extended.cpp",
            kind = "source",
            language = "c++",
        ),
    ],
    "src/oblib/lib:mock_di": [
        struct(
            path = "src/oblib/lib/stat/mock_diagnostic_info.cpp",
            kind = "source",
            language = "c++",
        ),
    ],
}
