#!/usr/bin/env python3
"""Verify cross-module runtime seams and reject process-global escape hatches."""

import re
import sys
from pathlib import Path


REQUIRED_DEFINITIONS = {
    "src/sql/ob_sql_utils.cpp": (
        r"\bObSQLNameService::check_and_convert_database_name\s*\(",
        r"\bObSQLNameService::check_and_convert_table_name\s*\(",
        r"\bObSQLNameService::resolve_table_name\s*\(",
    ),
    "src/sql/ob_sql_trans_control.cpp": (
        r"\bObSqlTransControl::build_tx_param\s*\(",
    ),
    "src/rootserver/ob_local_management_service.cpp": (
        r"\bObLocalManagementService::tablet_major_freeze\s*\(",
        r"\bObLocalManagementService::major_freeze\s*\(",
        r"\bObLocalManagementService::check_partition_exchange_schema_for_user\s*\(",
        r"\bint\s+report_column_checksum_response\s*\(",
        r"\bint\s+report_ddl_single_replica_response\s*\(",
        r"\bint\s+renew_ddl_task_lease\s*\(",
        r"\bint\s+rebuild_vector_index\s*\(",
        r"\bint\s+load_idempotent_ddl_tablet_slice_counts\s*\(",
    ),
    "src/observer/dbms_scheduler/ob_dbms_sched_service.cpp": (
        r"\bObDBMSSchedService::allocate_job_id\s*\(",
        r"\bObDBMSSchedService::create_job\s*\(",
        r"\bObDBMSSchedService::wakeup_scheduler\s*\(",
    ),
    "src/observer/ob_server.cpp": (
        r"\bObServer::get_or_insert_schedule_info\s*\(",
        r"\bObServer::read_timestamp_service\s*\(",
        r"\bObServer::check_current_tenant_available\s*\(",
        r"\bObServer::get_current_tenant_cpu\s*\(",
        r"\bObServer::get_current_tenant_min_worker_count\s*\(",
        r"\bObServer::get_current_worker_unit_min_cpu\s*\(",
        r"\bObServer::current_query_start_time\s*\(",
        r"\bObServer::request_ctas_cleanup\s*\(",
        r"\bObServer::submit_current_tenant_request\s*\(",
        r"\bObServer::submit_px_task\s*\(",
        r"\bObServer::create_virtual_table_factory\s*\(",
        r"\bObServer::destroy_virtual_table_factory\s*\(",
    ),
}

FORBIDDEN_RUNTIME_ESCAPE_PATTERNS = (
    r"\bquery::set_(?:ai_endpoint_resolver|scheduler_service|vector_index_service)\s*\(",
    r"\bquery::(?:ai_endpoint_resolver|scheduler_service|vector_index_service)\s*\(\s*\)",
)

FORBIDDEN_RUNTIME_REGISTRY_FILES = (
    "src/query/scheduler/ob_scheduler_service.cpp",
    "src/query/vector/ob_vector_index_service.cpp",
    "src/query/api/query/vector/ob_vector_index_service_registry.h",
)


def main():
    repo = (
        Path(sys.argv[1]).resolve()
        if len(sys.argv) > 1
        else Path(__file__).resolve().parents[2]
    )
    missing = []
    definition_count = 0
    for relative, patterns in REQUIRED_DEFINITIONS.items():
        text = (repo / relative).read_text(errors="ignore")
        for pattern in patterns:
            definition_count += 1
            if re.search(pattern, text, re.MULTILINE) is None:
                missing.append("%s -> %s" % (relative, pattern))

    forbidden = []
    for relative in FORBIDDEN_RUNTIME_REGISTRY_FILES:
        if (repo / relative).exists():
            forbidden.append("registry file remains: " + relative)

    source_root = repo / "src"
    for path in source_root.rglob("*"):
        if path.suffix not in (".h", ".hpp", ".cc", ".cpp"):
            continue
        text = path.read_text(errors="ignore")
        for pattern in FORBIDDEN_RUNTIME_ESCAPE_PATTERNS:
            if re.search(pattern, text, re.MULTILINE) is not None:
                forbidden.append(
                    "%s -> %s" % (path.relative_to(repo), pattern)
                )

    if missing or forbidden:
        for item in missing:
            print("[FAIL] missing runtime seam definition: " + item, file=sys.stderr)
        for item in forbidden:
            print("[FAIL] process-global runtime escape: " + item, file=sys.stderr)
        return 1

    print(
        "runtime seam definition check: %d definitions present, "
        "%d global escape patterns absent, %d registry files absent"
        % (
            definition_count,
            len(FORBIDDEN_RUNTIME_ESCAPE_PATTERNS),
            len(FORBIDDEN_RUNTIME_REGISTRY_FILES),
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
