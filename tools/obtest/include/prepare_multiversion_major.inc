# 1: bootstrap

ob1=OBI(cluster=1:1:2,proxy_num=0,init_unit_cpu=0);
ob1.reboot;

let $tenant_name = migrate_multiversion_major;

ob1.mysql alter system set enable_rebalance = 'false';
ob1.mysql alter system set default_compress_func = 'none';

let $proxy_ip = deploy_get_value(ob1.z1.obs0.ip);
let $proxy_port = deploy_get_value(ob1.z1.obs0.mysql_port);

ob_connect(sys_conn, $proxy_ip, root@sys,,oceanbase, $proxy_port);

# 2: create tenant

connection sys_conn;

source sql/physical_backup/prepare_multi_zone_resource.sql;
ob1.create_tenant CREATE TENANT $tenant_name resource_pool_list=('small_pool'), locality = "F@z1,F@z3", primary_zone = "z1" set ob_tcp_invited_nodes='%';

let $tenant_id = query_get_value(select tenant_id from oceanbase.__all_tenant where tenant_name = "$tenant_name", tenant_id, 1);
let $resource_pool_id = deploy_get_value(ob1.select select resource_pool_id from oceanbase.__all_resource_pool where tenant_id = $tenant_id, resource_pool_id, 1);

ob_connect(z1_obs0, ob1.z1.obs0, root@$tenant_name, , test, , USE_MYSQL_CONNECTOR);
set @@session.ob_query_timeout = 1000000000;

# 2.1: clearify the observer of z3

let $dst_ip0 = deploy_get_value(ob1.z3.obs0.ip);
let $dst_port0 = deploy_get_value(ob1.z3.obs0.port);

let $dst_ip1 = deploy_get_value(ob1.z3.obs1.ip);
let $dst_port1 = deploy_get_value(ob1.z3.obs1.port);

let $dst0 = $dst_ip0:$dst_port0;
let $dst1 = $dst_ip1:$dst_port1;

let $unit_id = deploy_get_value(ob1.select select unit_id from oceanbase.__all_unit where resource_pool_id = $resource_pool_id and zone = 'z3', unit_id, 1);
let $ip = deploy_get_value(ob1.select select svr_ip from oceanbase.__all_unit where unit_id = $unit_id, svr_ip, 1);
let $port = deploy_get_value(ob1.select select svr_port from oceanbase.__all_unit where unit_id = $unit_id, svr_port, 1);

let $cur_ip = $ip;
let $cur_port = $port;
let $cur = $ip:$port;

if ($cur == $dst0)
{
    let $cur_obs = ob1.z3.obs0;
    let $dst_obs = ob1.z3.obs1;
    let $dst_ip = $dst_ip1;
    let $dst_port = $dst_port1;
    let $dst = $dst_ip:$dst_port;
}
if ($cur == $dst1)
{
    let $cur_obs = ob1.z3.obs1;
    let $dst_obs = ob1.z3.obs0;
    let $dst_ip = $dst_ip0;
    let $dst_port = $dst_port0;
    let $dst = $dst_ip:$dst_port;
}

ob_connect(cur_conn, $cur_obs, root@$tenant_name, , test, , USE_MYSQL_CONNECTOR);


# 3: create table

connection z1_obs0;

create table t1(c1 bigint, f1 varchar(1024), f2 varchar(262144)) COMPRESSION='none';
create table t1_col(c1 bigint, f1 varchar(1024), f2 varchar(262144)) COMPRESSION='none';

let $t1_tablet_id = deploy_get_value(ob1.select select tablet_id from oceanbase.__all_virtual_table where tenant_id = $tenant_id and table_name = "t1", tablet_id, 1);

# insert data (15 macro blocks)
insert into t1 select
    uniform(1, 100, random()) c1,
    randstr(300, random()) f1,
    randstr(300, random()) f2
from table(generator(50000));

insert into t1_col select
    uniform(1, 100, random()) c1,
    randstr(300, random()) f1,
    randstr(300, random()) f2
from table(generator(50000));

connection sys_conn;

# trigger major freeze
ob1.mysql alter system major freeze;
sleep 10;
ob1.check_until_timeout select (frozen_scn - last_scn) as result from oceanbase.CDB_OB_MAJOR_COMPACTION where tenant_id=$tenant_id, 0, 600;

connection z1_obs0;

# insert data (6 macro blocks)
insert into t1 select
    uniform(101, 200, random()) c1,
    randstr(300, random()) f1,
    randstr(300, random()) f2
from table(generator(20000));

insert into t1_col select
    uniform(101, 200, random()) c1,
    randstr(300, random()) f1,
    randstr(300, random()) f2
from table(generator(20000));

connection sys_conn;

# trigger major freeze
ob1.mysql alter system major freeze;
sleep 10;
ob1.check_until_timeout select (frozen_scn - last_scn) as result from oceanbase.CDB_OB_MAJOR_COMPACTION where tenant_id=$tenant_id, 0, 600;

connection z1_obs0;

# insert data (3 macro blocks)
insert into t1 select
    uniform(201, 300, random()) c1,
    randstr(300, random()) f1,
    randstr(300, random()) f2
from table(generator(10000));

insert into t1_col select
    uniform(201, 300, random()) c1,
    randstr(300, random()) f1,
    randstr(300, random()) f2
from table(generator(10000));

connection sys_conn;

# trigger major freeze
ob1.mysql alter system major freeze;
sleep 10;
ob1.check_until_timeout select (frozen_scn - last_scn) as result from oceanbase.CDB_OB_MAJOR_COMPACTION where tenant_id=$tenant_id, 0, 600;

set @@session.ob_query_timeout = 10000000;

# tablet t1:
# major 1: 0 macro blocks
# major 2: 15 macro blocks
# major 3: 21 macro blocks (15 reused, 6 new)
# major 4: 24 macro blocks (21 reused, 3 new)
