"""Tracked and generated source inventory for the native PL module."""

PL_UNITY_GROUPS = [
    struct(
        name = "pl_common",
        language = "c++",
        srcs = [
            "src/pl/ob_pl.cpp",
            "src/pl/ob_pl_allocator.cpp",
            "src/pl/ob_pl_build.cpp",
            "src/pl/ob_pl_exception_handling.cpp",
            "src/pl/ob_pl_interface_pragma.cpp",
            "src/pl/ob_pl_interpreter.cpp",
            "src/pl/ob_pl_package.cpp",
            "src/pl/ob_pl_package_manager.cpp",
            "src/pl/ob_pl_package_state.cpp",
            "src/pl/ob_pl_package_guard.cpp",
            "src/pl/ob_pl_resolver.cpp",
            "src/pl/ob_pl_router.cpp",
            "src/pl/ob_pl_stmt.cpp",
            "src/pl/ob_pl_type.cpp",
            "src/pl/ob_pl_user_type.cpp",
        ],
    ),
    struct(
        name = "pl_cache",
        language = "c++",
        srcs = [
            "src/pl/pl_cache/ob_pl_cache.cpp",
            "src/pl/pl_cache/ob_pl_cache_mgr.cpp",
            "src/pl/pl_cache/ob_pl_cache_object.cpp",
            "src/pl/ob_pl_server_cursor.cpp",
        ],
    ),
    struct(
        name = "pl_recompile",
        language = "c++",
        srcs = [
            "src/pl/ob_pl_dependency_util.cpp",
        ],
    ),
    struct(
        name = "pl_sys_package",
        language = "c++",
        srcs = [
            "src/pl/sys_package/ob_dbms_stats.cpp",
            "src/pl/sys_package/ob_dbms_scheduler_mysql.cpp",
            "src/pl/sys_package/ob_dbms_application.cpp",
            "src/pl/sys_package/ob_dbms_session.cpp",
            "src/pl/sys_package/ob_dbms_monitor.cpp",
            "src/pl/sys_package/ob_dbms_space.cpp",
            "src/pl/sys_package/ob_dbms_limit_calculator_mysql.cpp",
            "src/pl/sys_package/ob_dbms_vector_mysql.cpp",
            "src/pl/sys_package/ob_dbms_hybrid_vector_mysql.cpp",
            "src/pl/sys_package/ob_dbms_ai_service.cpp",
            "src/pl/sys_package/ob_dbms_index_manager.cpp",
        ],
    ),
]

PL_STANDALONE_CXX_SOURCES = [
    "src/pl/parser/ob_pl_parser.cpp",
]

PL_STANDALONE_C_SOURCES = [
    "src/pl/parser/pl_non_reserved_keywords_mysql_mode.c",
    "src/pl/parser/pl_parser_base.c",
    "src/pl/parser/pl_parser_mysql_mode_lex.c",
    "src/pl/parser/pl_parser_mysql_mode_tab.c",
]
