"""Stable public interface and tracked private implementation headers for PL."""

PL_PUBLIC_HEADER_ROOTS = [
    "ob_pl.h",
    "ob_pl_allocator.h",
    "ob_pl_package.h",
    "ob_pl_package_manager.h",
    "ob_pl_stmt.h",
    "parser/ob_pl_parser.h",
    "pl_cache/ob_pl_cache_mgr.h",
    "sys_package/ob_dbms_stats.h",
]


PL_PRIVATE_HEADERS = [
    "ob_pl_build.h",
    "ob_pl_dependency_util.h",
    "ob_pl_exception_handling.h",
    "ob_pl_interface_pragma.h",
    "ob_pl_interpreter.h",
    "ob_pl_package_guard.h",
    "ob_pl_package_state.h",
    "ob_pl_resolver.h",
    "ob_pl_router.h",
    "ob_pl_type.h",
    "ob_pl_user_type.h",
    "parser/parse_stmt_item_type.h",
    "parser/parse_stmt_node.h",
    "parser/pl_parser_base.h",
    "pl_cache/ob_pl_cache.h",
    "pl_cache/ob_pl_cache_object.h",
    "sys_package/ob_dbms_ai_service.h",
    "sys_package/ob_dbms_application.h",
    "sys_package/ob_dbms_hybrid_vector_mysql.h",
    "sys_package/ob_dbms_index_manager.h",
    "sys_package/ob_dbms_limit_calculator_mysql.h",
    "sys_package/ob_dbms_monitor.h",
    "sys_package/ob_dbms_scheduler_mysql.h",
    "sys_package/ob_dbms_session.h",
    "sys_package/ob_dbms_space.h",
    "sys_package/ob_dbms_vector_mysql.h",
]

PL_IGNORED_GENERATED_HEADERS = [
    "parser/pl_parser_mysql_mode_lex.h",
    "parser/pl_parser_mysql_mode_tab.h",
    "parser/pl_parser_oracle_mode_lex.h",
    "parser/pl_parser_oracle_mode_tab.h",
]

def pl_validate_header_inventory(checked_in_headers):
    owner = {}
    for category, headers in [("public", PL_PUBLIC_HEADER_ROOTS), ("private", PL_PRIVATE_HEADERS)]:
        for path in headers:
            if path in owner:
                fail("PL header %s is owned by both %s and %s" % (path, owner[path], category))
            owner[path] = category
    checked_in = {path: True for path in checked_in_headers}
    missing = sorted([path for path in checked_in if path not in owner])
    stale = sorted([path for path in owner if path not in checked_in])
    if missing or stale:
        fail("PL header ownership differs from the tree: missing=%s stale=%s" % (missing, stale))
