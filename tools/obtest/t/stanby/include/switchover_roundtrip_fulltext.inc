#############################################
# owner: @nijia.nj
# description: Fulltext index workload for switchover roundtrip test
# Tests: table with fulltext index survives multiple switchovers
# Uses $switchover_step (1-8) to determine which phase to execute
#
# Data tracking:
#   Step 1: rows 1,2,3 (3 rows)
#   Step 3: rows 4,5 (5 rows)
#   Step 4: row 6 (6 rows)
#   Step 6: row 7 (7 rows)
#############################################

# Step 1: Create table with fulltext index and insert data (on primary db_p)
if ($switchover_step == 1)
{
    create table test_switchover_fts(id int primary key, title varchar(200), content text, fulltext index fts_idx(title, content));
    insert into test_switchover_fts values(1, 'OceanBase Database', 'OceanBase is a distributed relational database');
    insert into test_switchover_fts values(2, 'Cloud Computing', 'Cloud computing provides scalable resources');
    insert into test_switchover_fts values(3, 'Data Security', 'Data security protects sensitive information');
}

# Step 2: Verify initial data on standby (on standby db_s)
if ($switchover_step == 2)
{
    select count(*) from test_switchover_fts;
    select id, title from test_switchover_fts order by id;
}

# Step 3: Insert more data before switchover (on primary db_p)
if ($switchover_step == 3)
{
    insert into test_switchover_fts values(4, 'Machine Learning', 'Machine learning enables pattern recognition');
    insert into test_switchover_fts values(5, 'Deep Learning', 'Deep learning uses neural networks');
}

# Step 4: Insert data on new primary (on new primary db_s)
if ($switchover_step == 4)
{
    insert into test_switchover_fts values(6, 'Natural Language', 'NLP processes human language');
}

# Step 5: Verify data on new standby (on new standby db_p)
if ($switchover_step == 5)
{
    select count(*) from test_switchover_fts;
    select id, title from test_switchover_fts order by id;
}

# Step 6: Roundtrip DML on new primary (on new primary db_s)
if ($switchover_step == 6)
{
    insert into test_switchover_fts values(7, 'Computer Vision', 'Computer vision analyzes visual data');
}

# Step 7: Verify on original primary after roundtrip (on primary db_p)
if ($switchover_step == 7)
{
    select count(*) from test_switchover_fts;
}

# Step 8: Verify on original standby after roundtrip (on standby db_s)
if ($switchover_step == 8)
{
    select count(*) from test_switchover_fts;
}
