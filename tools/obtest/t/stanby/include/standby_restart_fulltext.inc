#############################################
# owner: @nijia.nj
# description: Fulltext index workload for standby restart test
# Tests: table with fulltext index survives standby/primary restart
# Uses $restart_step (1-8) to determine which phase to execute
#############################################

# Step 1: Create table with fulltext index and insert data (on primary)
if ($restart_step == 1)
{
    create table test_restart_fts(id int primary key, title varchar(200), content text, fulltext index fts_idx(title, content));
    insert into test_restart_fts values(1, 'OceanBase Database', 'OceanBase is a distributed relational database system');
    insert into test_restart_fts values(2, 'Cloud Computing', 'Cloud computing provides scalable resources on demand');
    insert into test_restart_fts values(3, 'Data Security', 'Data security protects sensitive information from threats');
}

# Step 2: Verify initial data on standby
if ($restart_step == 2)
{
    select count(*) from test_restart_fts;
    select id, title from test_restart_fts order by id;
}

# Step 3: Insert more data while standby is down (on primary)
if ($restart_step == 3)
{
    insert into test_restart_fts values(4, 'Machine Learning', 'Machine learning enables intelligent pattern recognition');
    insert into test_restart_fts values(5, 'Deep Learning', 'Deep learning uses neural networks for complex tasks');
}

# Step 4: Verify data after standby restart (on standby)
if ($restart_step == 4)
{
    select count(*) from test_restart_fts;
    select id, title from test_restart_fts order by id;
}

# Step 5: Write more data after standby restart (on primary)
if ($restart_step == 5)
{
    insert into test_restart_fts values(6, 'Natural Language', 'NLP processes and understands human language');
}

# Step 6: Verify continued sync on standby
if ($restart_step == 6)
{
    select count(*) from test_restart_fts;
}

# Step 7: Write data after primary restart (on primary)
if ($restart_step == 7)
{
    select count(*) from test_restart_fts;
    insert into test_restart_fts values(7, 'Computer Vision', 'Computer vision analyzes and interprets visual data');
}

# Step 8: Final verification on standby
if ($restart_step == 8)
{
    select count(*) from test_restart_fts;
    select id, title from test_restart_fts order by id;
}
