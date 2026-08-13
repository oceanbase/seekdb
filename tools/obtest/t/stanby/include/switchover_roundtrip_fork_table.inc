#############################################
# owner: @nijia.nj
# description: Fork table workload for switchover roundtrip test
# Tests: fork table DDL operations survive multiple switchovers
# Uses $switchover_step (1-8) to determine which phase to execute
#
# Data tracking:
#   test_switchover_fork_src (source table):
#     Step 1: create + insert 3 rows (c1=1,2,3) + fork to dst (by db_p)
#     Step 3: insert 2 more (c1=4,5) → 5 rows
#     Step 4: insert 1 (c1=6) → 6 rows + fork to dst2 (by db_s as new primary)
#     Step 6: insert 1 (c1=7) → 7 rows
#   test_switchover_fork_dst (forked from src in step 1, starts with 3 rows):
#     Step 3: insert 1 (c1=4) → 4 rows
#     Step 6: insert 1 (c1=5) → 5 rows
#   test_switchover_fork_dst2 (forked from src in step 4 by new primary, starts with 6 rows):
#     (no further modifications) → 6 rows
#############################################

# Step 1: Create source table, insert data, fork table (on primary db_p)
if ($switchover_step == 1)
{
    create table test_switchover_fork_src(c1 int primary key, c2 varchar(100), c3 int);
    insert into test_switchover_fork_src values(1, 'src_1', 100);
    insert into test_switchover_fork_src values(2, 'src_2', 200);
    insert into test_switchover_fork_src values(3, 'src_3', 300);
    db_p.mysql fork table oceanbase.test_switchover_fork_src to oceanbase.test_switchover_fork_dst;
    select count(*) from test_switchover_fork_dst;
}

# Step 2: Verify initial data on standby (on standby db_s)
if ($switchover_step == 2)
{
    select count(*) from test_switchover_fork_src;
    select c1, c2, c3 from test_switchover_fork_src order by c1;
    db_s.check_until_timeout select count(*) from test_switchover_fork_dst where c2 = 'src_1', 1, 120;
    select count(*) from test_switchover_fork_dst;
    select c1, c2, c3 from test_switchover_fork_dst order by c1;
}

# Step 3: Insert more data before switchover (on primary db_p)
if ($switchover_step == 3)
{
    insert into test_switchover_fork_src values(4, 'src_4', 400);
    insert into test_switchover_fork_src values(5, 'src_5', 500);
    insert into test_switchover_fork_dst values(4, 'fork_4', 444);
}

# Step 4: Insert data + fork on new primary (on new primary db_s)
if ($switchover_step == 4)
{
    insert into test_switchover_fork_src values(6, 'src_6', 600);
    db_s.mysql fork table oceanbase.test_switchover_fork_src to oceanbase.test_switchover_fork_dst2;
    select count(*) from test_switchover_fork_dst2;
}

# Step 5: Verify data on new standby (on new standby db_p)
if ($switchover_step == 5)
{
    select count(*) from test_switchover_fork_src;
    select c1, c2, c3 from test_switchover_fork_src order by c1;
    select count(*) from test_switchover_fork_dst;
    # 等待 fork 表 schema 同步（test_switchover_fork_dst2 在 step 4 由新主库 db_s 创建）
    db_p.check_until_timeout select count(*) from test_switchover_fork_dst2 where c2 = 'src_1', 1, 120;
    select count(*) from test_switchover_fork_dst2;
}

# Step 6: Roundtrip DML on new primary (on new primary db_s)
if ($switchover_step == 6)
{
    insert into test_switchover_fork_src values(7, 'src_7', 700);
    insert into test_switchover_fork_dst values(5, 'fork_5', 555);
}

# Step 7: Verify on original primary after roundtrip (on primary db_p)
if ($switchover_step == 7)
{
    select count(*) from test_switchover_fork_src;
    select count(*) from test_switchover_fork_dst;
    select count(*) from test_switchover_fork_dst2;
}

# Step 8: Verify on original standby after roundtrip (on standby db_s)
if ($switchover_step == 8)
{
    select count(*) from test_switchover_fork_src;
    select count(*) from test_switchover_fork_dst;
    select count(*) from test_switchover_fork_dst2;
}
