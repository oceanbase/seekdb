# include/wait_fork_table.inc
#
# SUMMARY
#
#    Waits for fork-table DDL tasks (DDL_FORK_TABLE=10009) in
#    `oceanbase.__all_virtual_ddl_task_status`.
#    Supports:
#    - wait all fork tasks to complete (default)
#    - wait a specific fork task (source table -> target table)
#    - wait until a specific fork task reaches a certain `status` (stage)
#
# USAGE
#
#    --source include/wait_fork_table.inc
#
#   OR
#
#    let $wait_timeout= 60; # Override default 30 seconds with 60.
#    --source include/wait_fork_table.inc
#
#   OR (wait specific A -> B fork task to complete, by table_id)
#
#    let $wait_src_table_id= <src_table_id>;
#    let $wait_dest_table_id= <dest_table_id>;
#    --source include/wait_fork_table.inc
#
#   OR (wait A -> B fork task reaches a given `status`, by table_id)
#
#    let $wait_src_table_id= <src_table_id>;
#    let $wait_dest_table_id= <dest_table_id>;
#    let $wait_status= <expected_status>;  # condition: status >= $wait_status
#    --source include/wait_fork_table.inc
#
#   OR (wait A -> B fork task reaches a given `status`, by status name)
#
#    let $wait_src_table_id= <src_table_id>;
#    let $wait_dest_table_id= <dest_table_id>;
#    let $wait_status= BUILD_DATA;           # BUILD_DATA / WAIT_DATA_COMPLEMENT / SUCCESS / FAIL
#    --source include/wait_fork_table.inc
#
# PARAMETERS
#
#    $wait_timeout      - Timeout in seconds (default: 30; consumed then reset to 0 by this include)
#    $wait_src_table_id - Source table_id (object_id) filter (optional; consumed then reset to 0 by this include)
#    $wait_dest_table_id- Target table_id (target_object_id) filter (optional; consumed then reset to 0 by this include)
#    $wait_task_cnt     - If set (>0), wait until at least this many matching tasks exist (optional; consumed then reset to 0).
#    $wait_status       - If set, wait until a matching task reaches this status (condition: status >= $wait_status).
#                         Only supports status name:
#                           PREPARE / WAIT_FROZE_END / BUILD_DATA / WAIT_DATA_COMPLEMENT / SUCCESS / FAIL
#                         instead of waiting tasks to disappear.
#

--disable_query_log
--disable_result_log

# fork ddl task status constants (see src/share/ob_ddl_common.h)
# Do not guard with "if" — callers should not override these mappings.
let $fork_status_prepare= 0;                      # PREPARE
let $fork_status_wait_froze_end= 19;              # WAIT_FROZE_END
let $fork_status_build_data= 48;                  # BUILD_DATA
let $fork_status_wait_data_complement= 49;        # WAIT_DATA_COMPLEMENT
let $fork_status_fail= 99;                        # FAIL
let $fork_status_success= 100;                    # SUCCESS

# Resolve filters (only supports explicit table_id filters)
# Note: we do NOT overwrite user variables like $src_table_id/$dest_table_id used by tests.
let $resolved_src_table_id= 0;
let $resolved_dest_table_id= 0;
let $resolved_wait_task_cnt= 0;

if ($wait_src_table_id)
{
  let $resolved_src_table_id= $wait_src_table_id;
}
if ($wait_dest_table_id)
{
  let $resolved_dest_table_id= $wait_dest_table_id;
}
if ($wait_task_cnt)
{
  let $resolved_wait_task_cnt= $wait_task_cnt;
}

# Reset input filter variables ASAP so they won't leak into subsequent cases,
# even if this include later exits early (e.g. timeout -> die).
let $wait_src_table_id= 0;
let $wait_dest_table_id= 0;
let $wait_task_cnt= 0;

# Set default timeout to 30 seconds if not specified
let $resolved_wait_timeout= 0;
if ($wait_timeout)
{
  let $resolved_wait_timeout= $wait_timeout;
}
# Reset $wait_timeout ASAP so that its value won't leak into subsequent calls/cases.
let $wait_timeout= 0;

let $wait_counter= 600;
if ($resolved_wait_timeout)
{
  let $wait_counter= `SELECT $resolved_wait_timeout * 10`;
}

# Wait mode:
# - default: wait until matching tasks disappear (count(*) == 0)
# - if $wait_status is set: wait until ALL matching tasks reach status
#   (and optionally wait until at least $wait_task_cnt tasks exist)
let $wait_mode= 0;
let $resolved_wait_status= -1;
let $resolved_wait_status_token= 0;
if ($wait_status)
{
  let $resolved_wait_status_token= $wait_status;
}
# Reset $wait_status ASAP so that its value won't leak into subsequent calls/cases.
let $wait_status= 0;

if ($resolved_wait_status_token)
{
  let $wait_mode= 1;
  # Only accept status name (string token), reject numeric input.
  let $resolved_wait_status= -1;
  if ($resolved_wait_status_token == PREPARE)
  {
    let $resolved_wait_status= $fork_status_prepare;
  }
  if ($resolved_wait_status_token == WAIT_FROZE_END)
  {
    let $resolved_wait_status= $fork_status_wait_froze_end;
  }
  if ($resolved_wait_status_token == BUILD_DATA)
  {
    let $resolved_wait_status= $fork_status_build_data;
  }
  if ($resolved_wait_status_token == WAIT_DATA_COMPLEMENT)
  {
    let $resolved_wait_status= $fork_status_wait_data_complement;
  }
  if ($resolved_wait_status_token == SUCCESS)
  {
    let $resolved_wait_status= $fork_status_success;
  }
  if ($resolved_wait_status_token == FAIL)
  {
    let $resolved_wait_status= $fork_status_fail;
  }
  if ($resolved_wait_status < 0)
  {
    --echo # Invalid wait_status. Only status names are supported:
    --echo #   PREPARE / WAIT_FROZE_END / BUILD_DATA / WAIT_DATA_COMPLEMENT / SUCCESS / FAIL
    die Invalid wait_status for wait_fork_table.inc;
  }
}

# Connect to system connection for querying system tables
connect (obsys_wait,$OBMYSQL_MS0,root@sys,,oceanbase,$OBMYSQL_PORT);
connection obsys_wait;

# Wait for fork table tasks to complete / reach status
let $wait_satisfied= 0;
let $fork_task_cnt= 0;
let $fork_task_all_reached= 0;
while ($wait_counter > 0)
{
  # Count matching tasks
  # - completion mode: count all matching tasks
  # - status mode: wait until ALL matching tasks reach target status
  if ($wait_mode == 0)
  {
    let $fork_task_cnt= `select count(*) from oceanbase.__all_virtual_ddl_task_status where ddl_type = 10009 and ($resolved_src_table_id = 0 or object_id = $resolved_src_table_id) and ($resolved_dest_table_id = 0 or target_object_id = $resolved_dest_table_id)`;
  }
  if ($wait_mode == 1)
  {
    # mysqltest treats SQL-assigned vars as strings, so do comparisons inside SQL and return a 0/1 flag.
    let $fork_task_all_reached= `select case when count(*) > 0 and ($resolved_wait_task_cnt = 0 or count(*) >= $resolved_wait_task_cnt) and ifnull(sum(case when status < $resolved_wait_status then 1 else 0 end), 0) = 0 then 1 else 0 end from oceanbase.__all_virtual_ddl_task_status where ddl_type = 10009 and ($resolved_src_table_id = 0 or object_id = $resolved_src_table_id) and ($resolved_dest_table_id = 0 or target_object_id = $resolved_dest_table_id)`;
  }

  if ($wait_mode == 0)
  {
    if ($fork_task_cnt == 0)
    {
      let $wait_satisfied= 1;
      let $wait_counter= 0;
    }
  }

  if ($wait_mode == 1)
  {
    if ($fork_task_all_reached == 1)
    {
      let $wait_satisfied= 1;
      let $wait_counter= 0;
    }
  }

  if ($wait_counter > 0)
  {
    real_sleep 0.1;
    dec $wait_counter;
  }
}

--enable_query_log
--enable_result_log

if ($wait_satisfied == 0)
{
  --echo # Warning: wait_fork_table.inc timed out
  --echo # Filters:
  --echo #   src_table_id=$resolved_src_table_id, dest_table_id=$resolved_dest_table_id
  --echo #   wait_mode=$wait_mode (0=complete, 1=status>=wait_status), wait_status=$resolved_wait_status
  --echo # Current matching ddl tasks:
  --eval select ddl_type, status, object_id, target_object_id, task_id, gmt_create, gmt_modified from oceanbase.__all_virtual_ddl_task_status where ddl_type = 10009 and ($resolved_src_table_id = 0 or object_id = $resolved_src_table_id) and ($resolved_dest_table_id = 0 or target_object_id = $resolved_dest_table_id) order by gmt_create desc, task_id desc
  disconnect obsys_wait;
  connection default;
  die Timeout waiting for ddl tasks (fork table) to complete / reach status;
}

disconnect obsys_wait;
connection default;

# Reset variables
let $fork_task_cnt=;
let $fork_task_all_reached=;
let $wait_satisfied=;
let $wait_mode=;
let $resolved_wait_status=;
let $resolved_wait_status_token=;
let $resolved_wait_timeout=;
let $resolved_wait_task_cnt=;
let $resolved_src_table_id=;
let $resolved_dest_table_id=;
let $wait_src_table_id= 0;
let $wait_dest_table_id= 0;
let $wait_task_cnt= 0;
