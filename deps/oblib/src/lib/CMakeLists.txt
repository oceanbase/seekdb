add_subdirectory(compress)
add_subdirectory(restore)

ob_set_subtarget(oblib_lib downsink_qt
  thread/ob_queue_thread.cpp
  thread/ob_balance_filter.cpp
)

ob_set_subtarget(oblib_lib downsink
  time/ob_clock_generator.cpp
  utility/ob_smart_call.cpp
  utility/ob_smart_var.cpp
  utility/ob_target_specific.cpp
  utility/ob_common_utility.cpp
  utility/data_buffer.cpp
)

ob_set_subtarget(oblib_lib charset
  charset/ob_ctype_bin.cc
  charset/ob_ctype.cc
  charset/ob_ctype_mb.cc
  charset/ob_ctype_simple.cc
  charset/ob_ctype_utf8.cc
  charset/ob_dtoa.cc
  charset/ob_charset.cpp
)


ob_set_subtarget(oblib_lib common
  ob_abort.cpp
  ob_date_unit_type.cpp
  ob_define.cpp
  ob_lib_config.cpp
  ob_name_id_def.cpp
  ob_running_mode.cpp
  runtime.cpp
  worker.cpp
)

ob_set_subtarget(oblib_lib geo
)


ob_set_subtarget(oblib_lib encode
  encode/ob_base64_encode.cpp
  encode/ob_quoted_printable_encode.cpp
  encode/ob_uuencode.cpp
)

ob_set_subtarget(oblib_lib codec
  codec/ob_fast_bp_func.cpp
  codec/ob_generated_scalar_bp_func.cpp
)

ob_set_subtarget(oblib_lib_simd codec_simd
  codec/ob_fast_delta.cpp
  codec/ob_generated_unalign_simd_bp_func.cpp
)

ob_lib_add_target(oblib_lib_simd)

if (${ARCHITECTURE} STREQUAL "x86_64")
  target_compile_options(oblib_lib_simd
    PRIVATE -mavx2 -mssse3
    )
elseif(${ARCHITECTURE} STREQUAL "amd64")
  target_compile_options(oblib_lib_simd
    PRIVATE
    -mavx2
    -mssse3
    -msse3
    -msse4.1
)
endif()

ob_set_subtarget(oblib_lib common_mixed
  atomic/ob_atomic_reference.cpp
  checksum/ob_crc64.cpp
  checksum/ob_parity_check.cpp
  container/ob_vector.ipp
  cpu/ob_cpu_topology.cpp
  encode/ob_base64_encode.cpp
  encode/ob_quoted_printable_encode.cpp
  encode/ob_uuencode.cpp
  encrypt/ob_encrypted_helper.cpp
  file/file_directory_utils.cpp
  file/ob_file.cpp
  file/ob_string_util.cpp
  hash/ob_link_hashmap.cpp
  hash/ob_linear_hash_map.cpp
  hash/ob_dchash.cpp
  hash/ob_hashutils.cpp
  hash_func/xxhash.c
  hash_func/murmur_hash.cpp
  json/ob_json.cpp
  json/ob_json_print_utils.cpp
  json/ob_yson.cpp
  lds/ob_lds_define.cpp
  net/ob_addr.cpp
  net/ob_net_util.cpp
  objectpool/ob_pool.ipp
  objectpool/ob_server_object_pool.cpp
  profile/ob_trace_id.cpp
  profile/ob_trace_id_adaptor.cpp
  thread/ob_dedup_queue.cpp
  queue/ob_lighty_queue.cpp
  queue/ob_link_queue.cpp
  queue/ob_ms_queue.cpp
  random/ob_random.cpp
  rc/context.cpp
  rc/ob_rc.cpp
  stat/ob_di_tls.cpp
  stat/ob_diagnose_info.cpp
  stat/ob_latch_define.cpp
  stat/ob_stat_template.cpp
  statistic_event/ob_stat_event.cpp
  string/ob_sql_string.cpp
  string/ob_string.cpp
  string/ob_strings.cpp
  string/ob_string_buffer.cpp
  task/ob_timer.cpp
  task/ob_timer_monitor.cpp
  task/ob_timer_service.cpp
  thread_local/ob_tsi_utils.cpp
  thread_local/thread_buffer.cpp
  time/Time.cpp
  time/ob_cur_time.cpp
  time/ob_time_utility.cpp
  time/ob_tsc_timestamp.cpp
  trace/ob_trace.cpp
  trace/ob_trace_event.cpp
  wait_event/ob_wait_class.cpp
  wait_event/ob_wait_event.cpp
  locale/ob_locale_type.cc
  locale/ob_locale.cpp
)

ob_set_subtarget(oblib_lib lock
  lock/cond.cpp
  lock/mutex.cpp
  lock/ob_bucket_lock.cpp
  lock/ob_latch.cpp
  lock/ob_thread_cond.cpp
  lock/ob_rwlock.cpp
  lock/ob_futex.cpp
  lock/ob_bucket_qsync_lock.cpp
  lock/ob_qsync_lock.cpp
)

ob_set_subtarget(oblib_lib mysqlclient
)

ob_set_subtarget(oblib_lib oblog
  oblog/ob_async_log_struct.cpp
  oblog/ob_base_log_buffer.cpp
  oblog/ob_base_log_writer.cpp
  oblog/ob_ringbuf_log_writer.cpp
  oblog/ob_log.cpp
  oblog/ob_log_compressor.cpp
  oblog/ob_log_dba_event.cpp
  oblog/ob_log_module.ipp
  oblog/ob_log_time_fmt.cpp
  oblog/ob_trace_log.cpp
  oblog/ob_warning_buffer.cpp
  oblog/ob_syslog_rate_limiter.cpp
)

ob_set_subtarget(oblib_lib signal
  signal/ob_signal_handlers.cpp
  signal/ob_signal_struct.cpp
  signal/ob_signal_utils.cpp
  signal/ob_libunwind.c
)

ob_set_subtarget(oblib_lib thread
  thread/ob_async_task_queue.cpp
  thread/ob_dynamic_thread_pool.cpp
  thread/ob_reentrant_thread.cpp
  thread/ob_simple_thread_pool.ipp
  thread/ob_map_queue_thread_pool.cpp
  thread/protected_stack_allocator.cpp
  thread/thread.cpp
  thread/threads.cpp
  thread/ob_thread_hook.cpp
  thread/ob_pthread.cpp
  thread/ob_thread_name.cpp
)

ob_set_subtarget(oblib_lib utility
  utility/ob_fast_convert.cpp
  utility/ob_hang_fatal_error.cpp
  utility/ob_platform_utils.cpp
  utility/ob_print_utils.cpp
  utility/ob_serialization_helper.cpp
  utility/ob_tracepoint.cpp
  utility/ob_utility.cpp
  utility/utility.cpp
  utility/ob_backtrace.cpp
  utility/ob_hyperloglog.cpp
)

ob_set_subtarget(oblib_lib ssl
)

ob_set_subtarget(oblib_lib ob_vector_util
  vector/ob_vector_util.cpp
  vector/ob_vsag_adaptor.cpp
)

ob_lib_add_target(oblib_lib)

ob_set_subtarget(oblib_lib_bitmap common
  container/ob_bitmap.cpp
)

ob_lib_add_target(oblib_lib_bitmap)

ob_set_subtarget(ob_malloc_object_list common_alloc
  alloc/abit_set.cpp
  utility/alloc_assist.cpp
  alloc/alloc_failed_reason.cpp
  alloc/alloc_func.cpp
  alloc/alloc_struct.cpp
  alloc/block_set.cpp
  alloc/memory_dump.cpp
  alloc/ob_malloc_allocator.cpp
  alloc/ob_malloc_callback.cpp
  alloc/ob_malloc_sample_struct.cpp
  alloc/ob_ctx_allocator.cpp
  alloc/object_mgr.cpp
  alloc/object_set.cpp
  resource/achunk_mgr.cpp
  resource/ob_resource_mgr.cpp
  allocator/ob_allocator_v2.cpp
  allocator/ob_block_alloc_mgr.cpp
  allocator/ob_concurrent_fifo_allocator.cpp
  alloc/ob_ctx_define.cpp
  allocator/ob_delay_free_allocator.cpp
  allocator/ob_fifo_allocator.cpp
  allocator/ob_hazard_ref.cpp
  allocator/ob_malloc.cpp
  utility/ob_mod_define.cpp
  allocator/ob_page_manager.cpp
  allocator/ob_slice_alloc.cpp
  allocator/ob_tc_malloc.cpp
)

ob_add_new_object_target(ob_malloc_object ob_malloc_object_list)
target_link_libraries(ob_malloc_object oblib_base_without_pass)
add_library(ob_malloc STATIC)
target_link_libraries(ob_malloc ob_malloc_object)

# malloc_hook is only available on Linux (uses Linux-specific syscall and weak alias)
if(NOT APPLE AND NOT WIN32 AND NOT ANDROID)
  add_library(malloc_hook STATIC
      alloc/malloc_hook.cpp
      alloc/malloc_hook.h
      alloc/malloc_hook_extended.cpp)
  target_link_libraries(malloc_hook oblib_base)
endif()

if(WIN32)
  set(VCPKG_LIB_DIR "${OB_VCPKG_DIR}/lib")
  target_link_libraries(oblib_lib
    PUBLIC ob_malloc compress restore
    ${VCPKG_LIB_DIR}/libxml2.lib
  )
else()
  target_link_libraries(oblib_lib
    PUBLIC ob_malloc compress restore
    ${DEP_DIR}/lib/libxml2.a
    ${DEP_DIR}/lib/liblzma.a
  )
endif()

if (ANDROID)
# vsag vector library -- Android: all libs installed to DEP_DIR/lib by ob-deps
target_link_directories(
  oblib_lib PUBLIC
  ${DEP_DIR}/lib
)
target_compile_options(oblib_lib PUBLIC -fopenmp)
target_link_libraries(oblib_lib
  PUBLIC
  libvsag_static.a
  libantlr4-autogen.a
  libantlr4-runtime.a
  libdiskann.a
  libsimd.a
  libcpuinfo.a
  libfmt.a
  libopenblas.a
  libspdlog.a
  libroaring.a
  -fopenmp -static-openmp
)
else()
# vsag vector library
target_link_directories(
  oblib_lib PUBLIC ${DEP_DIR}/lib ${DEP_DIR}/lib/vsag_lib
)
if(UNIX)
  target_compile_options(oblib_lib PUBLIC -fopenmp)
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  target_link_libraries(oblib_lib
    PUBLIC
    libcpuinfo.a
    libsimd.a
    libvsag_static.a
    libdiskann.a
    libopenblas.a
    libgfortran_static.a
    libquadmath_static.a
    libgomp_static.a
    libroaring.a
    libantlr4-runtime.a
    libantlr4-autogen.a
    libfmt.a
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  target_link_libraries(oblib_lib
    PUBLIC
    libcpuinfo.a
    libsimd.a
    libvsag_static.a
    libdiskann.a
    libopenblas.a
    libgfortran_static.a
    libgomp_static.a
    libroaring.a
    libantlr4-runtime.a
    libantlr4-autogen.a
    libfmt.a
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
  target_link_libraries(oblib_lib
    PUBLIC
    libcpuinfo.a
    libsimd.a
    libvsag_static.a
    libdiskann.a
    libio.a
    libgfortran_static.a
    libquadmath_static.a
    libgcc.a
    # libgomp.a
    libomp_static.a
    libopenblas.a
    libroaring.a
    libantlr4-runtime.a
    libantlr4-autogen.a
    libfmt.a
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
  target_link_directories(oblib_lib PUBLIC ${OB_VSAG_DIR}/lib)
  target_include_directories(oblib_lib PUBLIC ${OB_VSAG_DIR}/include)
  target_link_libraries(oblib_lib
    PUBLIC
    cpuinfo.lib
    simd.lib
    vsag_static.lib
    diskann.lib
    openblas.lib
    libomp.lib
    roaring.lib
    antlr4-runtime-static.lib
    antlr4-autogen.lib
    fmt.lib
  )
else()
  message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

endif()
# Use this stub only for targets that do not need diagnostic state, such as unit tests.
add_library(mock_di stat/mock_diagnostic_info.cpp)
target_include_directories(mock_di PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/deps/oblib/src)
