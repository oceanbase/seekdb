ob_set_subtarget(ob_storage vector_type
  vector_type/ob_vector_add.cpp
  vector_type/ob_vector_div.cpp
  vector_type/ob_vector_l2_distance.cpp
  vector_type/ob_vector_ip_distance.cpp
  vector_type/ob_vector_cosine_distance.cpp
  vector_type/ob_vector_l1_distance.cpp
  vector_type/ob_vector_norm.cpp
  vector_type/ob_vector_common_util.cpp
  vector_type/ob_sparse_vector_ip_distance.cpp
  vector_type/ob_vector_l2_similarity.cpp
  vector_type/ob_vector_cosine_similarity.cpp
  vector_type/ob_vector_ip_similarity.cpp
)

ob_set_subtarget(ob_storage deadlock
  deadlock/ob_deadlock_detector_common_define.cpp
  deadlock/ob_deadlock_detector_mgr.cpp
  deadlock/ob_deadlock_inner_table_service.cpp
  deadlock/ob_deadlock_key_wrapper.cpp
  deadlock/ob_lcl_scheme/ob_lcl_node.cpp
  deadlock/ob_lcl_scheme/ob_lcl_utils.cpp
)

ob_set_subtarget(ob_storage blocksstable
  blocksstable/ob_kvcache_pointer_swizzle.cpp
  blocksstable/ob_kvcache_pre_warmer.cpp
  blocksstable/ob_block_manager.cpp
  blocksstable/ob_macro_seq_generator.cpp
  blocksstable/ob_object_manager.cpp
  blocksstable/ob_block_sstable_struct.cpp
  blocksstable/ob_bloom_filter_cache.cpp
  blocksstable/ob_data_buffer.cpp
  blocksstable/ob_fuse_row_cache.cpp
  blocksstable/ob_imicro_block_reader.cpp
  blocksstable/ob_imicro_block_writer.cpp
  blocksstable/ob_micro_block_header.cpp
  blocksstable/ob_macro_block.cpp
  blocksstable/ob_macro_block_bare_iterator.cpp
  blocksstable/ob_macro_block_checker.cpp
  blocksstable/ob_macro_block_common_header.cpp
  blocksstable/ob_macro_block_flusher.cpp
  blocksstable/ob_macro_block_handle.cpp
  blocksstable/ob_macro_block_id.cpp
  blocksstable/ob_macro_block_meta_mgr.cpp
  blocksstable/ob_macro_block_meta.cpp
  blocksstable/ob_macro_block_reader.cpp
  blocksstable/ob_macro_block_struct.cpp
  blocksstable/ob_macro_block_writer.cpp
  blocksstable/ob_block_writer_concurrent_guard.cpp
  blocksstable/ob_data_macro_block_merge_writer.cpp
  blocksstable/ob_micro_block_cache.cpp
  blocksstable/ob_micro_block_checksum_helper.cpp
  blocksstable/ob_micro_block_hash_index.cpp
  blocksstable/ob_micro_block_reader.cpp
  blocksstable/ob_micro_block_row_getter.cpp
  blocksstable/ob_micro_block_row_lock_checker.cpp
  blocksstable/ob_micro_block_row_scanner.cpp
  blocksstable/ob_micro_block_writer.cpp
  blocksstable/ob_storage_object_handle.cpp
  blocksstable/ob_row_cache.cpp
  blocksstable/ob_row_queue.cpp
  blocksstable/ob_row_reader.cpp
  blocksstable/ob_row_writer.cpp
  blocksstable/ob_shared_macro_block_manager.cpp
  blocksstable/ob_sstable.cpp
  blocksstable/ob_sstable_macro_block_header.cpp
  blocksstable/ob_simplified_sstable_macro_block_header.cpp
  blocksstable/ob_sstable_meta.cpp
  blocksstable/ob_logic_macro_id.cpp
  blocksstable/ob_sstable_printer.cpp
  blocksstable/ob_storage_cache_suite.cpp
  blocksstable/ob_super_block_buffer_holder.cpp
  blocksstable/ob_storage_datum.cpp
  blocksstable/ob_datum_row.cpp
  blocksstable/ob_datum_rowkey.cpp
  blocksstable/ob_datum_row_store.cpp
  blocksstable/ob_datum_row_utils.cpp
  blocksstable/ob_data_store_desc.cpp
  blocksstable/ob_column_checksum_struct.cpp
  blocksstable/ob_table_flag.cpp
  blocksstable/ob_datum_rowkey_vector.cpp
  blocksstable/ob_batch_datum_rows.cpp
)

ob_set_subtarget(ob_storage blocksstable_encoding1
  blocksstable/encoding/ob_bit_stream.cpp
  blocksstable/encoding/ob_column_equal_decoder.cpp
  blocksstable/encoding/ob_column_equal_encoder.cpp
  blocksstable/encoding/ob_column_datum_iter.cpp
  blocksstable/encoding/ob_const_decoder.cpp
  blocksstable/encoding/ob_const_encoder.cpp
  blocksstable/encoding/ob_dict_decoder.cpp
  blocksstable/encoding/ob_dict_encoder.cpp
  blocksstable/encoding/ob_encoding_allocator.cpp
  blocksstable/encoding/ob_encoding_bitset.cpp
  blocksstable/encoding/ob_encoding_hash_util.cpp
  blocksstable/encoding/ob_encoding_util.cpp
  blocksstable/encoding/ob_hex_string_decoder.cpp
  blocksstable/encoding/ob_hex_string_encoder.cpp
  blocksstable/encoding/ob_icolumn_decoder.cpp
  blocksstable/encoding/ob_icolumn_encoder.cpp
  blocksstable/encoding/ob_new_column_decoder.cpp
  blocksstable/encoding/ob_integer_base_diff_decoder.cpp
  blocksstable/encoding/ob_integer_base_diff_encoder.cpp
)
# Vector-decode explicit-instantiation sites: must be ALONE (own TU), not unity, so the include
# guard does not swallow their definition pass and they remain the sole definers of the symbols.
ob_set_subtarget(ob_storage ALONE
  blocksstable/encoding/ob_vector_decode_util.cpp
  blocksstable/encoding/ob_vector_decode_util_b.cpp
  blocksstable/encoding/ob_vector_decode_util_c.cpp
  blocksstable/encoding/ob_vector_decode_util_d.cpp
  blocksstable/encoding/ob_vector_decode_util_e.cpp
)
ob_set_subtarget(ob_storage blocksstable_encoding2
  blocksstable/encoding/ob_inter_column_substring_decoder.cpp
  blocksstable/encoding/ob_inter_column_substring_encoder.cpp
  blocksstable/encoding/ob_imicro_block_decoder.cpp
  blocksstable/encoding/ob_micro_block_decoder.cpp
  blocksstable/encoding/ob_micro_block_encoder.cpp
  blocksstable/encoding/ob_multi_prefix_tree.cpp
  blocksstable/encoding/ob_raw_decoder.cpp
  blocksstable/encoding/ob_raw_encoder.cpp
  blocksstable/encoding/ob_rle_decoder.cpp
  blocksstable/encoding/ob_rle_encoder.cpp
  blocksstable/encoding/ob_string_diff_decoder.cpp
  blocksstable/encoding/ob_string_diff_encoder.cpp
  blocksstable/encoding/ob_string_prefix_decoder.cpp
  blocksstable/encoding/ob_string_prefix_encoder.cpp
  blocksstable/encoding/neon/ob_dict_decoder_neon.cpp
  blocksstable/encoding/neon/ob_raw_decoder_neon.cpp
)


ob_set_subtarget(ob_storage blocksstable_index_block
  blocksstable/index_block/ob_agg_row_struct.cpp
  blocksstable/index_block/ob_macro_meta_temp_store.cpp
  blocksstable/index_block/ob_index_block_aggregator.cpp
  blocksstable/index_block/ob_index_block_builder.cpp
  blocksstable/index_block/ob_index_block_macro_iterator.cpp
  blocksstable/index_block/ob_index_block_dual_meta_iterator.cpp
  blocksstable/index_block/ob_index_block_dumper.cpp
  blocksstable/index_block/ob_ddl_sstable_scan_merge.cpp
  blocksstable/index_block/ob_index_block_row_scanner.cpp
  blocksstable/index_block/ob_ddl_index_block_row_iterator.cpp
  blocksstable/index_block/ob_index_block_row_struct.cpp
  blocksstable/index_block/ob_index_block_tree_cursor.cpp
  blocksstable/index_block/ob_index_block_util.cpp
  blocksstable/index_block/ob_skip_index_filter_executor.cpp
  blocksstable/index_block/ob_sstable_index_scanner.cpp
  blocksstable/index_block/ob_sstable_meta_info.cpp
  blocksstable/index_block/ob_sstable_sec_meta_iterator.cpp
  blocksstable/index_block/ob_clustered_index_block_writer.cpp
  blocksstable/index_block/ob_index_block_bare_iterator.cpp
)

ob_set_subtarget(ob_storage meta_store
  meta_store/ob_server_storage_meta_service.cpp
  meta_store/ob_server_storage_meta_persister.cpp
  meta_store/ob_server_storage_meta_replayer.cpp
  meta_store/ob_storage_meta_io_util.cpp
  meta_store/ob_local_storage_meta_service.cpp
  meta_store/ob_local_storage_meta_persister.cpp
  meta_store/ob_local_storage_meta_replayer.cpp
)

ob_set_subtarget(ob_storage tmp_file
  tmp_file/ob_i_tmp_file.cpp
  tmp_file/ob_i_tmp_file_manager.cpp
  tmp_file/ob_shared_nothing_tmp_file.cpp
  tmp_file/ob_sn_tmp_file_manager.cpp
  tmp_file/ob_tmp_file_block_manager.cpp
  tmp_file/ob_tmp_file_cache.cpp
  tmp_file/ob_tmp_file_eviction_manager.cpp
  tmp_file/ob_tmp_file_flush_ctx.cpp
  tmp_file/ob_tmp_file_flush_list_iterator.cpp
  tmp_file/ob_tmp_file_flush_manager.cpp
  tmp_file/ob_tmp_file_flush_priority_manager.cpp
  tmp_file/ob_tmp_file_global.cpp
  tmp_file/ob_tmp_file_io_ctx.cpp
  tmp_file/ob_tmp_file_io_handle.cpp
  tmp_file/ob_tmp_file_io_info.cpp
  tmp_file/ob_tmp_file_meta_tree.cpp
  tmp_file/ob_tmp_file_manager.cpp
  tmp_file/ob_tmp_file_page_cache_controller.cpp
  tmp_file/ob_tmp_file_thread_wrapper.cpp
  tmp_file/ob_tmp_file_thread_job.cpp
  tmp_file/ob_tmp_file_write_buffer_pool.cpp
  tmp_file/ob_tmp_file_write_buffer_index_cache.cpp
  tmp_file/ob_tmp_file_write_buffer_pool_entry_array.cpp
)

ob_set_subtarget(ob_storage slog
  slog/ob_server_slog_writer.cpp
  slog/ob_storage_log_batch_header.cpp
  slog/ob_storage_log_entry.cpp
  slog/ob_storage_log_item.cpp
  slog/ob_storage_log_nop_log.cpp
  slog/ob_storage_log_reader.cpp
  slog/ob_storage_log_replayer.cpp
  slog/ob_storage_log_struct.cpp
  slog/ob_storage_log.cpp
  slog/ob_storage_log_write_buffer.cpp
  slog/ob_storage_log_writer.cpp
  slog/ob_storage_logger.cpp
)

ob_set_subtarget(ob_storage ckpt
  slog_ckpt/ob_linked_macro_block_reader.cpp
  slog_ckpt/ob_linked_macro_block_struct.cpp
  slog_ckpt/ob_linked_macro_block_writer.cpp
  slog_ckpt/ob_server_checkpoint_reader.cpp
  slog_ckpt/ob_server_checkpoint_slog_handler.cpp
  slog_ckpt/ob_server_checkpoint_writer.cpp
  slog_ckpt/ob_local_storage_checkpoint_slog_handler.cpp
  slog_ckpt/ob_local_storage_checkpoint_reader.cpp
  slog_ckpt/ob_local_storage_checkpoint_writer.cpp
  slog_ckpt/ob_server_snapshot_handler.cpp
  slog_ckpt/ob_tablet_replay_create_handler.cpp
)

ob_set_subtarget(ob_storage fts
  fts/ob_ik_ft_parser.cpp
  fts/ob_ngram2_ft_parser.cpp
  fts/ob_beng_ft_parser.cpp
  fts/ob_fts_doc_word_iterator.cpp
  fts/ob_fts_parser_property.cpp
  fts/ob_fts_parser_helper.cpp
  fts/ob_fts_parser_name.cpp
  fts/ob_fts_stop_word.cpp
  fts/ob_fts_struct.cpp
  fts/ob_ngram_ft_parser.cpp
  fts/ob_whitespace_ft_parser.cpp
  fts/dict/ob_dic_loader.cpp
  fts/dict/ob_gen_dic_loader.cpp
  fts/dict/ob_dic_lock.cpp
  fts/dict/ob_ik_utf8_dic_loader.cpp
  fts/utils/unicode_utils.cpp
  fts/utils/ob_ft_ngram_impl.cpp
)

ob_set_subtarget(ob_storage ik
  fts/ik/ob_ik_processor.cpp
  fts/ik/ob_ik_letter_processor.cpp
  fts/ik/ob_ik_cjk_processor.cpp
  fts/ik/ob_ik_quantifier_processor.cpp
  fts/ik/ob_ik_surrogate_processor.cpp
  fts/ik/ob_ik_arbitrator.cpp
  fts/ik/ob_ik_token.cpp
  fts/dict/ob_ik_dic.cpp
  fts/dict/ob_ft_dat_dict.cpp
  fts/dict/ob_ft_dict_def.cpp
  fts/dict/ob_ft_trie.cpp
  fts/dict/ob_ft_cache_dict.cpp
  fts/dict/ob_ft_dict_hub.cpp
  fts/dict/ob_ft_range_dict.cpp
  fts/dict/ob_ft_dict_table_iter.cpp
  fts/dict/ob_ft_cache.cpp
)



ob_set_subtarget(ob_storage restore
)

ob_set_subtarget(ob_storage tablet
  tablet/ob_batch_create_tablet_arg.cpp
  tablet/ob_batch_create_tablet_pretty_arg.cpp
  tablet/ob_full_tablet_creator.cpp
  tablet/ob_i_tablet_mds_interface.cpp
  tablet/ob_i_tablet_mds_customized_interface.cpp
  tablet/ob_mds_range_query_iterator.cpp
  tablet/ob_mds_row_iterator.cpp
  tablet/ob_mds_scan_param_helper.cpp
  tablet/ob_mds_schema_helper.cpp
  tablet/ob_tablet_binding_helper.cpp
  tablet/ob_tablet_binding_replay_executor.cpp
  tablet/ob_tablet_complex_addr.cpp
  tablet/ob_tablet_create_delete_helper.cpp
  tablet/ob_tablet_create_replay_executor.cpp
  tablet/ob_tablet_create_sstable_param.cpp
  tablet/ob_tablet_ddl_info.cpp
  tablet/ob_tablet_id_set.cpp
  tablet/ob_tablet_iterator.cpp
  tablet/ob_tablet_memtable_mgr.cpp
  tablet/ob_tablet_meta.cpp
  tablet/ob_tablet_multi_source_data.cpp
  tablet/ob_tablet_service_clog_replay_executor.cpp
  tablet/ob_tablet_status.cpp
  tablet/ob_tablet_table_store.cpp
  tablet/ob_tablet_table_store_iterator.cpp
  tablet/ob_table_store_util.cpp
  tablet/ob_tablet_table_store_flag.cpp
  tablet/ob_tablet_create_delete_mds_user_data.cpp
  tablet/ob_tablet_create_mds_helper.cpp
  tablet/ob_tablet_delete_mds_helper.cpp
  tablet/ob_tablet_delete_replay_executor.cpp
  tablet/ob_tablet_ddl_complete_replay_executor.cpp
  tablet/ob_tablet_full_medium_info.cpp
  tablet/ob_tablet_full_memory_mds_data.cpp
  tablet/ob_tablet_mds_data.cpp
  tablet/ob_tablet_mds_data_cache.cpp
  tablet/ob_tablet_dumped_medium_info.cpp
  tablet/ob_tablet_binding_info.cpp
  tablet/ob_tablet_binding_mds_user_data.cpp
  tablet/ob_tablet_mds_node_dump_operator.cpp
  tablet/ob_tablet_mds_table_mini_merger.cpp
  tablet/ob_tablet_fork_mds_helper.cpp
  tablet/ob_tablet_persister.cpp
  tablet/ob_tablet_obj_load_helper.cpp
  tablet/ob_tablet.cpp
  tablet/ob_tablet_block_header.cpp
  tablet/ob_tablet_medium_info_reader.cpp
  tablet/ob_tablet_space_usage.cpp
  tablet/ob_tablet_block_aggregated_info.cpp
  tablet/ob_tablet_macro_info_iterator.cpp
  tablet/ob_tablet_ddl_complete_mds_helper.cpp
  tablet/ob_tablet_ddl_complete_mds_data.cpp
)

ob_set_subtarget(ob_storage tx
  tx/ob_ctx_tx_data.cpp
  tx/ob_gti_source.cpp
  tx/ob_id_service.cpp
  tx/ob_keep_alive_ls_handler.cpp
  tx/ob_tx_loop_worker.cpp
  tx/ob_tx_log_cb_define.cpp
  tx/ob_tx_log_cb_mgr.cpp
  tx/ob_tx_ctx_log_cb_helper.cpp
  tx/ob_timestamp_access.cpp
  tx/ob_timestamp_service.cpp
  tx/ob_weak_read_util.cpp
  tx/ob_time_wheel.cpp
  tx/ob_trans_ctx.cpp
  tx/ob_trans_ctx_lock.cpp
  tx/ob_trans_ctx_mgr.cpp
  tx/ob_trans_deadlock_adapter.cpp
  tx/ob_trans_ctx_mgr_v4.cpp
  tx/ob_trans_define.cpp
  tx/ob_trans_define_v4.cpp
  tx/ob_trans_end_trans_callback.cpp
  tx/ob_trans_event.cpp
  tx/ob_trans_factory.cpp
  tx/ob_trans_id_service.cpp
  tx/ob_trans_memory_stat.cpp
  tx/ob_tx_ctx.cpp
  tx/ob_defensive_check_mgr.cpp
  tx/ob_trans_result.cpp
  tx/ob_tx_result_struct.cpp
  tx/ob_trans_service.cpp
  tx/ob_trans_service_v4.cpp
  tx/ob_tx_api.cpp
  tx/ob_tx_elr_util.cpp
  tx/ob_tx_elr_handler.cpp
  tx/ob_tx_stat.cpp
  tx/ob_ls_tx_ctx_mgr_stat.cpp
  tx/ob_trans_submit_log_cb.cpp
  tx/ob_trans_timer.cpp
  tx/ob_trans_version_mgr.cpp
  tx/ob_ts_mgr.cpp
  tx/ob_tx_timestamp_waiter.cpp
  tx/ob_tx_data_define.cpp
  tx/ob_tx_data_op.cpp
  tx/ob_tx_data_functor.cpp
  tx/ob_tx_print_time_guard.cpp
  tx/ob_tx_log.cpp
  tx/ob_tx_log_adapter.cpp
  tx/ob_tx_big_segment_buf.cpp
  tx/ob_tx_on_demand_print.cpp
  tx/ob_tx_replay_executor.cpp
  tx/ob_tx_ctx_mds.cpp
  tx/ob_multi_data_source.cpp
  tx/ob_multi_data_source_printer.cpp
  tx/ob_multi_data_source_tx_buffer_node.cpp
)

ob_set_subtarget(ob_storage allocator
  allocator/ob_fifo_arena.cpp
  allocator/ob_memstore_allocator.cpp
  allocator/ob_handle_list.cpp
  allocator/ob_mds_allocator.cpp
  allocator/ob_shared_memory_allocator_mgr.cpp
  allocator/ob_tx_data_allocator.cpp
  allocator/ob_vector_allocator.cpp
)

ob_set_subtarget(ob_storage throttle
  throttle/ob_share_throttle_define.cpp
  throttle/ob_throttle_common.cpp
)

ob_set_subtarget(ob_storage tx_storage
  tx_storage/ob_server_mem_limit_getter.cpp
  tx_storage/ob_access_service.cpp
  tx_storage/ob_checkpoint_service.cpp
  tx_storage/ob_ls_freeze_thread.cpp
  tx_storage/ob_ls_service.cpp
  tx_storage/ob_memstore_freezer.cpp
  tx_storage/ob_memstore_freezer_common.cpp
  tx_storage/ob_memstore_freezer_local_dispatch.cpp
  tx_storage/ob_memory_printer.cpp
  tx_storage/ob_tablet_gc_service.cpp
  tx_storage/ob_empty_shell_task.cpp
)

ob_set_subtarget(ob_storage tx_table
  tx_table/ob_tx_ctx_memtable.cpp
  tx_table/ob_tx_ctx_memtable_mgr.cpp
  tx_table/ob_tx_ctx_table.cpp
  tx_table/ob_tx_data_cache.cpp
  tx_table/ob_tx_data_hash_map.cpp
  tx_table/ob_tx_data_memtable.cpp
  tx_table/ob_tx_data_memtable_mgr.cpp
  tx_table/ob_tx_data_table.cpp
  tx_table/ob_tx_table.cpp
  tx_table/ob_tx_table_define.cpp
  tx_table/ob_tx_table_guards.cpp
  tx_table/ob_tx_table_interface.cpp
  tx_table/ob_tx_table_iterator.cpp
)

ob_set_subtarget(ob_storage ls
  ls/ob_freezer.cpp
  ls/ob_ls_wrs_handler.cpp
  ls/ob_ls.cpp
  ls/ob_ls_ddl_log_handler.cpp
  ls/ob_ls_lock.cpp
  ls/ob_ls_meta.cpp
  ls/ob_ls_state.cpp
  ls/ob_ls_sync_tablet_seq_handler.cpp
  ls/ob_ls_switch_checker.cpp
  ls/ob_ls_tablet_service.cpp
  ls/ob_ls_tx_service.cpp
  ls/ob_ls_saved_info.cpp
  ls/ob_ls_reserved_snapshot_mgr.cpp
  ls/ob_ls_storage_clog_handler.cpp
)

ob_set_subtarget(ob_storage access
  access/ob_table_estimator.cpp
  access/ob_index_sstable_estimator.cpp
  access/ob_sstable_multi_version_row_iterator.cpp
  access/ob_sstable_row_getter.cpp
  access/ob_sstable_row_lock_checker.cpp
  access/ob_sstable_row_multi_getter.cpp
  access/ob_sstable_row_multi_scanner.cpp
  access/ob_sstable_row_whole_scanner.cpp
  access/ob_block_batched_row_store.cpp
  access/ob_block_row_store.cpp
  access/ob_vector_store.cpp
  access/ob_aggregated_store.cpp
  access/ob_aggregated_store_vec.cpp
  access/ob_scan_merge_loser_tree.cpp
  access/ob_single_merge.cpp
  access/ob_sstable_index_filter.cpp
  access/ob_multiple_get_merge.cpp
  access/ob_multiple_merge.cpp
  access/ob_multiple_multi_scan_merge.cpp
  access/ob_multiple_scan_merge.cpp
  access/ob_advance_scan_helper.cpp
  access/ob_table_scan_iterator.cpp
  access/ob_store_row_iterator.cpp
  access/ob_i_sample_iterator.cpp
  access/ob_block_sample_iterator.cpp
  access/ob_ddl_block_sample_iterator.cpp
  access/ob_row_sample_iterator.cpp
  access/ob_rows_info.cpp
  access/ob_sample_iter_helper.cpp
  access/ob_table_read_info.cpp
  access/ob_table_param.cpp
  access/ob_sample_filter.cpp
  access/ob_table_scan_range.cpp
  access/ob_table_access_param.cpp
  access/ob_table_access_context.cpp
  access/ob_fuse_row_cache_fetcher.cpp
  access/ob_dml_param.cpp
  access/ob_micro_block_handle_mgr.cpp
  access/ob_aggregate_base.cpp
  access/ob_pushdown_aggregate.cpp
  access/ob_pushdown_aggregate_vec.cpp
  access/ob_empty_read_bucket.cpp
  access/ob_global_iterator_pool.cpp
  access/ob_where_optimizer.cpp
)

ob_set_subtarget(ob_storage ddl
  ddl/ob_ddl_common_storage_impl.cpp
  ddl/ob_build_index_task.cpp
  ddl/ob_complement_data_task.cpp
  ddl/ob_delete_lob_meta_row_task.cpp
  ddl/ob_ddl_clog.cpp
  ddl/ob_ddl_merge_task.cpp
  ddl/ob_table_fork_info.cpp
  ddl/ob_tablet_fork_task.cpp
  ddl/ob_tablet_copy_util.cpp
  ddl/ob_ddl_redo_log_replayer.cpp
  ddl/ob_ddl_redo_log_writer.cpp
  ddl/ob_ddl_struct.cpp
  ddl/ob_direct_load_struct.cpp
  ddl/ob_direct_insert_sstable_ctx.cpp
  ddl/ob_ddl_redo_log_row_iterator.cpp
  ddl/ob_tablet_barrier_log.cpp
  ddl/ob_tablet_ddl_kv.cpp
  ddl/ob_tablet_ddl_kv_mgr.cpp
  ddl/ob_ddl_lock.cpp
  ddl/ob_ddl_replay_executor.cpp
  ddl/ob_ddl_heart_beat_task.cpp
  ddl/ob_ddl_seq_generator.cpp
  ddl/ob_i_direct_load_mgr.cpp
  ddl/ob_direct_load_mgr_v3.cpp
  ddl/ob_direct_load_mgr_utils.cpp
  ddl/ob_tablet_slice_row_iterator.cpp
  ddl/ob_ddl_independent_dag.cpp
  ddl/ob_ddl_insert_dag.cpp
  ddl/ob_ddl_macro_block_writer.cpp
  ddl/ob_ddl_macro_block_write_task.cpp
  ddl/ob_lob_macro_block_writer.cpp
  ddl/ob_ddl_dag_thread_pool.cpp
  ddl/ob_tablet_slice_writer.cpp
  ddl/ob_ddl_tablet_context.cpp
  ddl/ob_pipeline.cpp
  ddl/ob_ddl_pipeline.cpp
  ddl/ob_ddl_row_tmp_file.cpp
  ddl/ob_ddl_merge_task_utils.cpp
  ddl/ob_ddl_merge_task_v2.cpp
  ddl/ob_ddl_merge_helper.cpp
  ddl/ob_ddl_merge_schedule.cpp
  ddl/ob_hnsw_embedmgr.cpp
  ddl/ob_ddl_batch_rows.cpp
  ddl/ob_ddl_continuous_vector.cpp
  ddl/ob_ddl_discrete_vector.cpp
  ddl/ob_ddl_fixed_length_vector.cpp
  ddl/ob_ddl_vector.cpp
)

ob_set_subtarget(ob_storage common
  ob_storage_rpc_arg.cpp
  ob_order_perserving_encoder.cpp
  ob_table_dml_param.cpp
  ob_i_pre_warmer.cpp
  ob_tablet_autoincrement_service.cpp
  ob_row_bitmap.cpp
  ob_all_micro_block_range_iterator.cpp
  ob_bloom_filter_task.cpp
  ob_col_map.cpp
  ob_disk_usage_reporter.cpp
  ob_dml_running_ctx.cpp
  ob_file_system_router.cpp
  ob_i_memtable_mgr.cpp
  ob_i_store.cpp
  ob_i_table.cpp
  ob_i_tablet_memtable.cpp
  ob_partition_range_spliter.cpp
  ob_query_iterator_factory.cpp
  ob_relative_table.cpp
  ob_row_fuse.cpp
  ob_row_reshape.cpp
  ob_storage_schema.cpp
  ob_storage_schema_util.cpp
  ob_storage_schema_recorder.cpp
  ob_storage_clog_recorder.cpp
  ob_storage_struct.cpp
  ob_tablet_local_status.cpp
  ob_storage_table_guard.cpp
  ob_storage_util.cpp
  ob_super_block_struct.cpp
  ob_sync_tablet_seq_clog.cpp
  ob_table_store_stat_mgr.cpp
  ob_tablet_autoinc_seq_rpc_handler.cpp
  ob_value_row_iterator.cpp
  ob_common_id_utils.cpp
  ob_tablet_stat_mgr.cpp
  ob_protected_memtable_mgr_handle.cpp
  ob_gc_upper_trans_helper.cpp
)

ob_set_subtarget(ob_storage common_mixed
  checkpoint/ob_checkpoint_executor.cpp
  checkpoint/ob_data_checkpoint.cpp
  checkpoint/ob_freeze_checkpoint.cpp
  meta_mem/ob_external_tablet_cnt_map.cpp
  meta_mem/ob_flying_tablet_pointer_map.cpp
  meta_mem/ob_meta_obj_struct.cpp
  meta_mem/ob_storage_meta_cache.cpp
  meta_mem/ob_tablet_handle.cpp
  meta_mem/ob_tablet_map_key.cpp
  meta_mem/ob_tablet_pointer_map.cpp
  meta_mem/ob_tablet_pointer_handle.cpp
  meta_mem/ob_tablet_pointer.cpp
  meta_mem/ob_storage_meta_mem_mgr.cpp
  meta_mem/ob_storage_meta_obj_pool.cpp
  blockstore/ob_object_reader_writer.cpp
  concurrency_control/ob_multi_version_garbage_collector.cpp
  concurrency_control/ob_trans_stat_row.cpp
)

ob_set_subtarget(ob_storage compaction
  compaction/ob_table_ckm_items.cpp
  compaction/ob_batch_exec_dag.cpp
  compaction/ob_column_checksum_calculator.cpp
  compaction/ob_index_block_micro_iterator.cpp
  compaction/ob_i_compaction_filter.cpp
  compaction/ob_partition_merge_fuser.cpp
  compaction/ob_partition_merge_iter.cpp
  compaction/ob_partition_merge_progress.cpp
  compaction/ob_partition_merger.cpp
  compaction/ob_partition_merge_policy.cpp
  compaction/ob_partition_parallel_merge_ctx.cpp
  compaction/ob_tablet_merge_ctx.cpp
  compaction/ob_basic_tablet_merge_ctx.cpp
  compaction/ob_tablet_merge_task.cpp
  compaction/ob_tablet_merge_info.cpp
  compaction/ob_freeze_info_mgr.cpp
  compaction/ob_tablet_scheduler.cpp
  compaction/ob_tablet_scheduler_task_mgr.cpp
  compaction/ob_compaction_schedule_iterator.cpp
  compaction/ob_compaction_schedule_util.cpp
  compaction/ob_schedule_dag_func.cpp
  compaction/ob_extra_medium_info.cpp
  compaction/ob_medium_compaction_func.cpp
  compaction/ob_medium_compaction_mgr.cpp
  compaction/ob_medium_compaction_info.cpp
  compaction/ob_compaction_diagnose.cpp
  compaction/ob_compaction_tablet_diagnose.cpp
  compaction/ob_compaction_suggestion.cpp
  compaction/ob_compaction_trans_cache.cpp
  compaction/ob_medium_checker.cpp
  compaction/ob_sstable_merge_info_mgr.cpp
  compaction/ob_compaction_progress.cpp
  compaction/ob_server_compaction_event_history.cpp
  compaction/ob_compaction_util.cpp
  compaction/ob_partition_rows_merger.cpp
  compaction/ob_medium_list_checker.cpp
  compaction/ob_sstable_builder.cpp
  compaction/ob_compaction_memory_pool.cpp
  compaction/ob_compaction_memory_context.cpp
  compaction/ob_sstable_merge_history.cpp
  compaction/ob_batch_freeze_tablets_dag.cpp
  compaction/ob_progressive_merge_helper.cpp
  compaction/ob_medium_loop.cpp
  compaction/ob_basic_schedule_tablet_func.cpp
  compaction/ob_schedule_tablet_func.cpp
  compaction/ob_runtime_status_cache.cpp
  compaction/ob_schedule_status_cache.cpp
  compaction/ob_mds_filter_info.cpp
)

ob_set_subtarget(ob_storage compaction_filter
  compaction/filter/ob_tx_data_minor_filter.cpp
  compaction/filter/ob_mds_info_compaction_filter.cpp
)

ob_set_subtarget(ob_storage memtable
  memtable/ob_lock_wait_mgr.cpp
  memtable/ob_memtable.cpp
  memtable/ob_memtable_key.cpp
  memtable/ob_memtable_compact_writer.cpp
  memtable/ob_memtable_context.cpp
  memtable/ob_memtable_iterator.cpp
  memtable/ob_memtable_mutator.cpp
  memtable/ob_memtable_read_row_util.cpp
  memtable/ob_memtable_single_row_reader.cpp
  memtable/ob_redo_log_generator.cpp
  memtable/ob_row_compactor.cpp
  memtable/ob_row_conflict_handler.cpp
  memtable/ob_concurrent_control.cpp
)

ob_set_subtarget(ob_storage memtable_mvcc
  memtable/mvcc/ob_multi_version_iterator.cpp
  memtable/mvcc/ob_mvcc_acc_ctx.cpp
  memtable/mvcc/ob_mvcc_ctx.cpp
  memtable/mvcc/ob_mvcc_engine.cpp
  memtable/mvcc/ob_mvcc_iterator.cpp
  memtable/mvcc/ob_mvcc_row.cpp
  memtable/mvcc/ob_mvcc_trans_ctx.cpp
  memtable/mvcc/ob_tx_callback_list.cpp
  memtable/mvcc/ob_query_engine.cpp
  memtable/mvcc/ob_btree_iter_cache.cpp
  memtable/mvcc/ob_row_data.cpp
  memtable/mvcc/ob_mvcc_define.cpp
)

ob_set_subtarget(ob_storage multi_data_source
  multi_data_source/buffer_ctx.cpp
  multi_data_source/mds_ctx.cpp
  multi_data_source/mds_for_each_map_flush_operation.cpp
  multi_data_source/mds_node.cpp
  multi_data_source/mds_table_mgr.cpp
  multi_data_source/mds_table_order_flusher.cpp
  multi_data_source/mds_table_base.cpp
  multi_data_source/mds_writer.cpp
  multi_data_source/mds_table_handler.cpp
  multi_data_source/adapter_define/mds_dump_kv_wrapper.cpp
  multi_data_source/adapter_define/mds_dump_node.cpp
  multi_data_source/runtime_utility/common_define.cpp
  multi_data_source/runtime_utility/mds_service.cpp
  multi_data_source/runtime_utility/mds_factory.cpp
  multi_data_source/runtime_utility/mds_retry_control.cpp
  multi_data_source/ob_mds_table_merge_dag.cpp
  multi_data_source/ob_mds_table_merge_dag_param.cpp
  multi_data_source/ob_mds_table_merge_task.cpp
  multi_data_source/ob_mds_minor_compaction_filter.cpp
  multi_data_source/ob_tablet_create_mds_ctx.cpp
  multi_data_source/ob_tablet_mds_merge_ctx.cpp
  multi_data_source/mds_key_serialize_util.cpp
)

ob_set_subtarget(ob_storage tablelock
  tablelock/ob_lock_executor.cpp
  tablelock/ob_lock_func_executor.cpp
  tablelock/ob_lock_memtable.cpp
  tablelock/ob_lock_memtable_mgr.cpp
  tablelock/ob_lock_inner_connection_util.cpp
  tablelock/ob_lock_table.cpp
  tablelock/ob_lock_utils.cpp
  tablelock/ob_mem_ctx_table_lock.cpp
  tablelock/ob_mysql_lock_table_executor.cpp
  tablelock/ob_obj_lock.cpp
  tablelock/ob_table_lock_callback.cpp
  tablelock/ob_table_lock_common.cpp
  tablelock/ob_table_lock_deadlock.cpp
  tablelock/ob_table_lock_iterator.cpp
  tablelock/ob_table_lock_rpc_struct.cpp
  tablelock/ob_table_lock_service.cpp
  tablelock/ob_table_lock_local_executor.cpp
  tablelock/ob_table_lock_live_detector.cpp
)

ob_set_subtarget(ob_storage_extra ALONE
  ddl/ob_ddl_vector_fixed.cpp
  ddl/ob_ddl_vector_continuous.cpp
  ddl/ob_ddl_vector_discrete.cpp
  ddl/ob_ddl_vector_uniform.cpp
  ddl/ob_ddl_vector_uniform_const.cpp
  ddl/ob_ddl_vector_utils.cpp
)

ob_set_subtarget(ob_storage lob
  lob/ob_lob_meta.cpp
  lob/ob_lob_manager.cpp
  lob/ob_lob_seq.cpp
  lob/ob_lob_piece.cpp
  lob/ob_lob_persistent_adaptor.cpp
  lob/ob_lob_data_reader.cpp
  lob/ob_lob_locator.cpp
  lob/ob_lob_util.cpp
  lob/ob_lob_rpc_struct.cpp
  lob/ob_ext_info_callback.cpp
  lob/ob_lob_access_param.cpp
  lob/ob_lob_persistent_reader.cpp
  lob/ob_lob_persistent_iterator.cpp
  lob/ob_lob_write_buffer.cpp
  lob/ob_lob_cursor.cpp
  lob/ob_lob_location.cpp
  lob/ob_lob_remote.cpp
  lob/ob_lob_iterator.cpp
  lob/ob_lob_handler.cpp
  lob/ob_lob_meta_manager.cpp
  lob/ob_lob_retry.cpp
  lob/ob_lob_tablet_dml.cpp
  lob/ob_lob_locator_struct.cpp
)

ob_set_subtarget(ob_storage vector_index
  vector_index/cmd/ob_vector_refresh_index_executor.cpp
  vector_index/ob_vector_index_refresh.cpp
  vector_index/ob_vector_refresh_idx_transaction.cpp
  vector_index/ob_vector_index_sched_job_utils.cpp
)

ob_set_subtarget(ob_storage truncate_info
  truncate_info/ob_truncate_info_array.cpp
  truncate_info/ob_truncate_info.cpp
  truncate_info/ob_truncate_info_mds_helper.cpp
  truncate_info/ob_tablet_truncate_info_reader.cpp
  truncate_info/ob_truncate_partition_filter.cpp
  truncate_info/ob_mds_info_distinct_mgr.cpp
  truncate_info/ob_truncate_info_kv_cache.cpp
)

ob_set_subtarget(ob_storage retrieval
  retrieval/ob_block_max_iter.cpp
  retrieval/ob_block_stat_collector.cpp
  retrieval/ob_block_stat_iter.cpp
  retrieval/ob_inv_idx_param_estimator.cpp
  retrieval/ob_sparse_bmw_iter.cpp
  retrieval/ob_sparse_daat_iter.cpp
  retrieval/ob_sparse_taat_iter.cpp
  retrieval/ob_sparse_utils.cpp
  retrieval/ob_sparse_lookup_iter.cpp
  retrieval/ob_text_retrieval_token_iter.cpp
  retrieval/ob_text_daat_iter.cpp
  retrieval/ob_text_taat_iter.cpp
  retrieval/ob_spiv_daat_iter.cpp
  retrieval/ob_spiv_dim_iter.cpp
)

ob_add_new_object_target(ob_storage ob_storage)
ob_add_new_object_target(ob_storage_extra ob_storage_extra)

target_link_libraries(ob_storage PUBLIC ob_base)
target_link_libraries(ob_storage_extra PUBLIC ob_base)


add_library(ob_storage_static
  STATIC
  EXCLUDE_FROM_ALL)

target_link_libraries(ob_storage_static
  PUBLIC ob_storage ob_storage_extra)

ob_set_subtarget(ob_storage_simd common
  blocksstable/encoding/ob_raw_decoder_simd.cpp
  blocksstable/encoding/ob_dict_decoder_simd.cpp
)

ob_server_add_target(ob_storage_simd)

if (${ARCHITECTURE} STREQUAL "x86_64" OR ${ARCHITECTURE} STREQUAL "amd64")
  target_compile_options(ob_storage_simd
    PRIVATE
      -mtune=core-avx2 -mavx2 -mfma -mbmi2 -mavx512vl -mavx512bw
  )
endif()
