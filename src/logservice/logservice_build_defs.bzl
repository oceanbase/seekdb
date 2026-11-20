"""Logservice-owned source and Unity topology."""

LOGSERVICE_UNITY_GROUPS = [
    struct(
        name = "ob_logservice_common_0",
        language = "c++",
        srcs = [
            "src/logservice/ob_append_callback.cpp",
            "src/logservice/ob_log_handler.cpp",
            "src/logservice/ob_log_service.cpp",
            "src/logservice/ob_server_log_block_mgr.cpp",
            "src/logservice/ob_log_monitor.cpp",
        ],
    ),
    struct(
        name = "ob_logservice_common_mixed_0",
        language = "c++",
        srcs = [
            "src/logservice/applyservice/ob_log_apply_service.cpp",
            "src/logservice/localservice/ob_local_log_handler_set.cpp",
            "src/logservice/replayservice/ob_log_replay_service.cpp",
            "src/logservice/replayservice/ob_replay_handler.cpp",
            "src/logservice/replayservice/ob_replay_status.cpp",
        ],
    ),
    struct(
        name = "ob_logservice_common_util_0",
        language = "c++",
        srcs = [
            "src/logservice/common_util/ob_log_time_utils.cpp",
            "src/logservice/ob_log_allocator.cpp",
            "src/logservice/ob_log_allocator_mgr.cpp",
        ],
    ),
    struct(
        name = "ob_logservice_palf_0",
        language = "c++",
        srcs = [
            "src/logservice/palf/log_entry.cpp",
            "src/logservice/palf/log_entry_header.cpp",
            "src/logservice/palf/log_group_entry.cpp",
            "src/logservice/palf/log_group_entry_header.cpp",
            "src/logservice/palf/log_meta.cpp",
            "src/logservice/palf/log_meta_info.cpp",
        ],
    ),
    struct(
        name = "ob_logservice_palf_1",
        language = "c++",
        srcs = [
            "src/logservice/palf/palf_callback_wrapper.cpp",
            "src/logservice/palf/palf_env.cpp",
            "src/logservice/palf/palf_env_impl.cpp",
            "src/logservice/palf/palf_handle.cpp",
            "src/logservice/palf/palf_handle_impl.cpp",
            "src/logservice/palf/log_io_utils.cpp",
            "src/logservice/palf/log_io_context.cpp",
        ],
    ),
]
