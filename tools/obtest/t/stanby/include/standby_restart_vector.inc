#############################################
# owner: @nijia.nj
# description: Vector type + vector index workload for standby restart test
# Tests: table with vector column and vector index survives standby/primary restart
# Uses $restart_step (1-8) to determine which phase to execute
#
# Data tracking:
#   test_restart_vec (vector(3), with hnsw index):
#     Step 1: rows 1,2,3 (3 rows) + create vector index
#     Step 3: rows 4,5 (5 rows)
#     Step 5: row 6 (6 rows)
#     Step 7: row 7 (7 rows)
#   test_restart_vec_idx (vector(4), with hnsw index, created in step 3):
#     Step 3: create table + index + rows 1,2 (2 rows)
#     Step 5: row 3 (3 rows)
#     Step 7: row 4 (4 rows)
#############################################

# Step 1: Create table with vector column + vector index, insert data (on primary)
if ($restart_step == 1)
{
    create table test_restart_vec(id int primary key, name varchar(100), embedding vector(3));
    create vector index vec_idx_restart on test_restart_vec(embedding) with (distance=l2, type=hnsw, lib=vsag);
    insert into test_restart_vec values(1, 'vec_init_1', '[1.0, 2.0, 3.0]');
    insert into test_restart_vec values(2, 'vec_init_2', '[4.0, 5.0, 6.0]');
    insert into test_restart_vec values(3, 'vec_init_3', '[7.0, 8.0, 9.0]');
}

# Step 2: Verify initial data on standby
if ($restart_step == 2)
{
    select count(*) from test_restart_vec;
    select id, name from test_restart_vec order by id;
}

# Step 3: Insert more data + create new table with vector index while standby is down (on primary)
if ($restart_step == 3)
{
    insert into test_restart_vec values(4, 'vec_extra_4', '[10.0, 11.0, 12.0]');
    insert into test_restart_vec values(5, 'vec_extra_5', '[13.0, 14.0, 15.0]');
    create table test_restart_vec_idx(id int primary key, label varchar(100), feat vector(4));
    create vector index vec_idx_restart2 on test_restart_vec_idx(feat) with (distance=l2, type=hnsw, lib=vsag);
    insert into test_restart_vec_idx values(1, 'idx_1', '[1.0, 0.0, 0.0, 0.0]');
    insert into test_restart_vec_idx values(2, 'idx_2', '[0.0, 1.0, 0.0, 0.0]');
}

# Step 4: Verify data after standby restart (on standby)
if ($restart_step == 4)
{
    select count(*) from test_restart_vec;
    select id, name from test_restart_vec order by id;
    # 等待新表 schema 同步（test_restart_vec_idx 在备库停机期间创建）
    --disable_query_log
    --disable_result_log
    db_s.check_until_timeout select count(*) from test_restart_vec_idx where label = 'idx_1', 1, 120;
    --enable_query_log
    --enable_result_log
    select count(*) from test_restart_vec_idx;
    select id, label from test_restart_vec_idx order by id;
}

# Step 5: Write more data after standby restart (on primary)
if ($restart_step == 5)
{
    insert into test_restart_vec values(6, 'vec_cont_6', '[16.0, 17.0, 18.0]');
    insert into test_restart_vec_idx values(3, 'idx_3', '[0.0, 0.0, 1.0, 0.0]');
}

# Step 6: Verify continued sync on standby
if ($restart_step == 6)
{
    select count(*) from test_restart_vec;
    select count(*) from test_restart_vec_idx;
}

# Step 7: Write data after primary restart (on primary)
if ($restart_step == 7)
{
    select count(*) from test_restart_vec;
    insert into test_restart_vec values(7, 'vec_primary_7', '[19.0, 20.0, 21.0]');
    insert into test_restart_vec_idx values(4, 'idx_4', '[0.0, 0.0, 0.0, 1.0]');
}

# Step 8: Final verification on standby
if ($restart_step == 8)
{
    select count(*) from test_restart_vec;
    select id, name from test_restart_vec order by id;
    select count(*) from test_restart_vec_idx;
    select id, label from test_restart_vec_idx order by id;
}
