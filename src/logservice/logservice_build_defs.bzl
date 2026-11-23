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
        name = "ob_logservice_wal_1",
        language = "c++",
        srcs = [
            "src/logservice/wal_adapter/callback_wrapper.cpp",
            "src/logservice/wal_adapter/env.cpp",
            "src/logservice/wal_adapter/handle.cpp",
            "src/logservice/wal_adapter/handle_impl.cpp",
        ],
    ),
]
