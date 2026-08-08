--disable_query_log
--disable_result_log

let $dbname = obdb;
let $tbname = obtable;

select effective_tenant();
select current_user();
select user();
select session_user();
select system_user();

eval CREATE DATABASE $dbname;
eval USE $dbname;

# check curr database
select database();


eval CREATE TABLE $dbname.$tbname (i1 int, i2 int, v1 varchar(1024),v2 varchar(1024), n1 numeric, n2 numeric, f1 float, d1 double, t1 timestamp,t2 timestamp, PRIMARY KEY(i1,v1,n1,f1,t1)) ;

let $cnt = 10;
while($cnt >0)
{
    let $cnt1=math($cnt+1);
    eval INSERT INTO  $dbname.$tbname VALUES($cnt,$cnt1,'$cntvarchar1','$cntvarchar2',$cnt.$cnt,$cnt1.$cnt1,$cnt,$cnt1,CURRENT_TIMESTAMP(),CURRENT_TIMESTAMP());
    dec $cnt;
}

eval alter table $dbname.$tbname drop i2;
eval alter table $dbname.$tbname drop v2;
eval alter table $dbname.$tbname drop n2;
eval alter table $dbname.$tbname drop d1;

eval select * from $dbname.$tbname;

eval alter table $dbname.$tbname add column d1 double default '1.0000000001';
eval alter table $dbname.$tbname add column x2 varchar(10) default null;

eval SELECT count(*) FROM $dbname.$tbname;

### drop
eval DROP TABLE $dbname.$tbname;
eval DROP DATABASE $dbname;

--enable_query_log
--enable_result_log
