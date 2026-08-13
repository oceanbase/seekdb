#############################################
# owner: @nijia.nj
# description: DDL workload for switchover roundtrip test
# Tests: create table, alter table add column, create new table across switchovers
# Uses $switchover_step (1-8) to determine which phase to execute
#
# Data tracking:
#   Step 1: rows 1,2,3 (3 rows)
#   Step 3: row 4 + add extra_info column + update row 1 (4 rows)
#   Step 4: row 5 + create test_switchover_ddl_new with 1 row (5 rows + 1 new table row)
#   Step 6: row 6 + insert new table row 2 (6 rows + 2 new table rows)
#############################################

# Step 1: Create table + insert initial data (on primary db_p)
if ($switchover_step == 1)
{
    create table if not exists test_switchover_ddl(id int primary key, name varchar(100), value int, create_time timestamp default current_timestamp);
    insert into test_switchover_ddl values(1, 'test1', 100, current_timestamp);
    insert into test_switchover_ddl values(2, 'test2', 200, current_timestamp);
    insert into test_switchover_ddl values(3, 'test3', 300, current_timestamp);
}

# Step 2: Verify initial data on standby (on standby db_s)
if ($switchover_step == 2)
{
    select count(*) from test_switchover_ddl;
    select id, name, value from test_switchover_ddl order by id;
}

# Step 3: Pre-switchover DML + DDL (on primary db_p)
if ($switchover_step == 3)
{
    insert into test_switchover_ddl values(4, 'test4', 400, current_timestamp);
    alter table test_switchover_ddl add column extra_info varchar(200);
    update test_switchover_ddl set extra_info = 'updated' where id = 1;
}

# Step 4: Post-switchover DML + DDL on new primary (on new primary db_s)
if ($switchover_step == 4)
{
    insert into test_switchover_ddl(id, name, value, create_time, extra_info) values(5, 'test5', 500, current_timestamp, NULL);
    create table test_switchover_ddl_new(a int primary key, b varchar(100));
    insert into test_switchover_ddl_new values(1, 'new_table_data');
}

# Step 5: Verify data on new standby (on new standby db_p)
if ($switchover_step == 5)
{
    select count(*) from test_switchover_ddl;
    select id, name, value from test_switchover_ddl order by id;
    select id, name, value, extra_info from test_switchover_ddl where id = 1;
    # 等待新表 schema 同步（test_switchover_ddl_new 在 step 4 由新主库创建）
    --disable_query_log
    --disable_result_log
    db_p.check_until_timeout select count(*) from test_switchover_ddl_new where b = 'new_table_data', 1, 120;
    --enable_query_log
    --enable_result_log
    select count(*) from test_switchover_ddl_new;
    select * from test_switchover_ddl_new;
}

# Step 6: Roundtrip DML on new primary (on new primary db_s)
if ($switchover_step == 6)
{
    insert into test_switchover_ddl(id, name, value, create_time, extra_info) values(6, 'test6', 600, current_timestamp, NULL);
    insert into test_switchover_ddl_new values(2, 'roundtrip_data');
}

# Step 7: Verify on original primary after roundtrip (on primary db_p)
if ($switchover_step == 7)
{
    select count(*) from test_switchover_ddl;
    select count(*) from test_switchover_ddl_new;
}

# Step 8: Verify on original standby after roundtrip (on standby db_s)
if ($switchover_step == 8)
{
    select count(*) from test_switchover_ddl;
    select count(*) from test_switchover_ddl_new;
}
