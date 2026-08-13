#############################################
# owner: @nijia.nj
# description: DDL workload for standby restart test
# Tests: create table, alter table add column, create new table
# Uses $restart_step (1-8) to determine which phase to execute
#############################################

# Step 1: Create tables and insert initial data (on primary)
if ($restart_step == 1)
{
    create table if not exists test_restart_ddl(id int primary key, name varchar(100), value int);
    insert into test_restart_ddl values(1, 'ddl_init_1', 100);
    insert into test_restart_ddl values(2, 'ddl_init_2', 200);
    insert into test_restart_ddl values(3, 'ddl_init_3', 300);
}

# Step 2: Verify initial data on standby
if ($restart_step == 2)
{
    select count(*) from test_restart_ddl;
    select id, name, value from test_restart_ddl order by id;
}

# Step 3: Write data + DDL while standby is down (on primary)
if ($restart_step == 3)
{
    insert into test_restart_ddl values(4, 'ddl_extra_4', 400);
    insert into test_restart_ddl values(5, 'ddl_extra_5', 500);
    alter table test_restart_ddl add column extra_col varchar(100);
    update test_restart_ddl set extra_col = 'modified' where id = 1;
    create table test_restart_ddl_new(a int primary key, b varchar(100));
    insert into test_restart_ddl_new values(1, 'new_1');
    insert into test_restart_ddl_new values(2, 'new_2');
}

# Step 4: Verify data after standby restart (on standby, with schema wait)
if ($restart_step == 4)
{
    # 等待 schema 刷新（extra_col 列在备库停机期间通过 DDL 添加）
    --disable_query_log
    --disable_result_log
    db_s.check_until_timeout select count(*) from test_restart_ddl where extra_col = 'modified', 1, 120;
    --enable_query_log
    --enable_result_log
    select count(*) from test_restart_ddl;
    select id, name, value from test_restart_ddl order by id;
    select id, name, value, extra_col from test_restart_ddl where id = 1;
    select count(*) from test_restart_ddl_new;
    select * from test_restart_ddl_new order by a;
}

# Step 5: Write more data after standby restart (on primary)
if ($restart_step == 5)
{
    insert into test_restart_ddl(id, name, value, extra_col) values(6, 'ddl_cont_6', 600, 'new');
    insert into test_restart_ddl_new values(3, 'new_3');
}

# Step 6: Verify continued sync on standby
if ($restart_step == 6)
{
    select count(*) from test_restart_ddl;
    select count(*) from test_restart_ddl_new;
}

# Step 7: Write data after primary restart (on primary)
if ($restart_step == 7)
{
    select count(*) from test_restart_ddl;
    select count(*) from test_restart_ddl_new;
    insert into test_restart_ddl(id, name, value, extra_col) values(7, 'ddl_primary_7', 700, 'primary_restarted');
    insert into test_restart_ddl_new values(4, 'new_4');
}

# Step 8: Final verification on standby
if ($restart_step == 8)
{
    select count(*) from test_restart_ddl;
    select id, name, value from test_restart_ddl order by id;
    select count(*) from test_restart_ddl_new;
    select * from test_restart_ddl_new order by a;
}
