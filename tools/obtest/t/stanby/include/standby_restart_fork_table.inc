#############################################
# owner: @nijia.nj
# description: Fork table workload for standby restart test
# Tests: fork table DDL operations survive standby/primary restart
# Uses $restart_step (1-8) to determine which phase to execute
#
# Data tracking:
#   test_restart_fork_src (source table):
#     Step 1: create + insert 3 rows (c1=1,2,3) + fork to dst
#     Step 3: insert 2 more (c1=4,5) → 5 rows + fork to dst2
#     Step 5: insert 1 more (c1=6) → 6 rows
#     Step 7: insert 1 more (c1=7) → 7 rows
#   test_restart_fork_dst (forked from src in step 1, starts with 3 rows):
#     Step 3: insert 1 (c1=4) → 4 rows
#     Step 5: insert 1 (c1=5) → 5 rows
#   test_restart_fork_dst2 (forked from src in step 3, starts with 5 rows):
#     (no further modifications) → 5 rows
#############################################

# Step 1: Create source table, insert data, fork table (on primary)
if ($restart_step == 1)
{
    create table test_restart_fork_src(c1 int primary key, c2 varchar(100), c3 int);
    insert into test_restart_fork_src values(1, 'src_1', 100);
    insert into test_restart_fork_src values(2, 'src_2', 200);
    insert into test_restart_fork_src values(3, 'src_3', 300);
    db_p.mysql fork table oceanbase.test_restart_fork_src to oceanbase.test_restart_fork_dst;
    select count(*) from test_restart_fork_dst;
}

# Step 2: Verify source and forked table data on standby
if ($restart_step == 2)
{
    select count(*) from test_restart_fork_src;
    select c1, c2, c3 from test_restart_fork_src order by c1;
    db_s.check_until_timeout select count(*) from test_restart_fork_dst where c2 = 'src_1', 1, 120;
    select count(*) from test_restart_fork_dst;
    select c1, c2, c3 from test_restart_fork_dst order by c1;
}

# Step 3: Insert data + fork again while standby is down (on primary)
if ($restart_step == 3)
{
    insert into test_restart_fork_src values(4, 'src_4', 400);
    insert into test_restart_fork_src values(5, 'src_5', 500);
    insert into test_restart_fork_dst values(4, 'fork_4', 444);
    db_p.mysql fork table oceanbase.test_restart_fork_src to oceanbase.test_restart_fork_dst2;
}

# Step 4: Verify all tables after standby restart (on standby)
if ($restart_step == 4)
{
    select count(*) from test_restart_fork_src;
    select c1, c2, c3 from test_restart_fork_src order by c1;
    select count(*) from test_restart_fork_dst;
    # 等待 fork 表 schema 同步（test_restart_fork_dst2 在备库停机期间通过 fork table 创建）
    db_s.check_until_timeout select count(*) from test_restart_fork_dst2 where c2 = 'src_1', 1, 120;
    select count(*) from test_restart_fork_dst2;
}

# Step 5: Write more data (on primary)
if ($restart_step == 5)
{
    insert into test_restart_fork_src values(6, 'src_6', 600);
    insert into test_restart_fork_dst values(5, 'fork_5', 555);
}

# Step 6: Verify continued sync on standby
if ($restart_step == 6)
{
    select count(*) from test_restart_fork_src;
    select count(*) from test_restart_fork_dst;
    select count(*) from test_restart_fork_dst2;
}

# Step 7: Write data after primary restart (on primary)
if ($restart_step == 7)
{
    select count(*) from test_restart_fork_src;
    insert into test_restart_fork_src values(7, 'src_7', 700);
}

# Step 8: Final verification on standby
if ($restart_step == 8)
{
    select count(*) from test_restart_fork_src;
    select c1, c2, c3 from test_restart_fork_src order by c1;
    select count(*) from test_restart_fork_dst;
    select c1, c2, c3 from test_restart_fork_dst order by c1;
    select count(*) from test_restart_fork_dst2;
    select c1, c2, c3 from test_restart_fork_dst2 order by c1;
}
