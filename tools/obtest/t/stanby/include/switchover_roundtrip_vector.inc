#############################################
# owner: @nijia.nj
# description: Vector type + vector index workload for switchover roundtrip test
# Tests: table with vector column and vector index survives multiple switchovers
# Uses $switchover_step (1-8) to determine which phase to execute
#
# Data tracking:
#   test_switchover_vec (vector(3), with hnsw index):
#     Step 1: rows 1,2,3 (3 rows) + create vector index
#     Step 3: rows 4,5 (5 rows)
#     Step 4: row 6 (6 rows)
#     Step 6: row 7 (7 rows)
#   test_switchover_vec_idx (vector(4), with hnsw index, created in step 4):
#     Step 4: create table + index + rows 1,2 (2 rows)
#     Step 6: row 3 (3 rows)
#############################################

# Step 1: Create table with vector column + vector index, insert data (on primary db_p)
if ($switchover_step == 1)
{
    create table test_switchover_vec(id int primary key, name varchar(100), embedding vector(3));
    create vector index vec_idx_switchover on test_switchover_vec(embedding) with (distance=l2, type=hnsw, lib=vsag);
    insert into test_switchover_vec values(1, 'vec_1', '[1.0, 2.0, 3.0]');
    insert into test_switchover_vec values(2, 'vec_2', '[4.0, 5.0, 6.0]');
    insert into test_switchover_vec values(3, 'vec_3', '[7.0, 8.0, 9.0]');
}

# Step 2: Verify initial data on standby (on standby db_s)
if ($switchover_step == 2)
{
    select count(*) from test_switchover_vec;
    select id, name from test_switchover_vec order by id;
}

# Step 3: Insert more data before switchover (on primary db_p)
if ($switchover_step == 3)
{
    insert into test_switchover_vec values(4, 'vec_4', '[10.0, 11.0, 12.0]');
    insert into test_switchover_vec values(5, 'vec_5', '[13.0, 14.0, 15.0]');
}

# Step 4: Insert data + create new table with vector index on new primary (on new primary db_s)
if ($switchover_step == 4)
{
    insert into test_switchover_vec values(6, 'vec_6', '[16.0, 17.0, 18.0]');
    create table test_switchover_vec_idx(id int primary key, label varchar(100), feat vector(4));
    create vector index vec_idx_switchover2 on test_switchover_vec_idx(feat) with (distance=l2, type=hnsw, lib=vsag);
    insert into test_switchover_vec_idx values(1, 'vidx_1', '[1.0, 0.0, 0.0, 0.0]');
    insert into test_switchover_vec_idx values(2, 'vidx_2', '[0.0, 1.0, 0.0, 0.0]');
}

# Step 5: Verify data on new standby (on new standby db_p)
if ($switchover_step == 5)
{
    select count(*) from test_switchover_vec;
    select id, name from test_switchover_vec order by id;
    # 等待新表 schema 同步（test_switchover_vec_idx 在 step 4 由新主库创建）
    --disable_query_log
    --disable_result_log
    db_p.check_until_timeout select count(*) from test_switchover_vec_idx where label = 'vidx_1', 1, 120;
    --enable_query_log
    --enable_result_log
    select count(*) from test_switchover_vec_idx;
    select id, label from test_switchover_vec_idx order by id;
}

# Step 6: Roundtrip DML on new primary (on new primary db_s)
if ($switchover_step == 6)
{
    insert into test_switchover_vec values(7, 'vec_7', '[19.0, 20.0, 21.0]');
    insert into test_switchover_vec_idx values(3, 'vidx_3', '[0.0, 0.0, 1.0, 0.0]');
}

# Step 7: Verify on original primary after roundtrip (on primary db_p)
if ($switchover_step == 7)
{
    select count(*) from test_switchover_vec;
    select count(*) from test_switchover_vec_idx;
}

# Step 8: Verify on original standby after roundtrip (on standby db_s)
if ($switchover_step == 8)
{
    select count(*) from test_switchover_vec;
    select count(*) from test_switchover_vec_idx;
}
