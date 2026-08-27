[prepare]
table_num = 1
table_names = test
# create table test (i1 int, s1 varchar, f1 float,d1 datetime, i2 int, s2 varchar, f2 float , d2 datetime , primary key(i1,s1,f1,d1));

test = i1(1,1);i2(1,1);s1('s1',1);s2('s2',1);f1(1,1);f2(1,1);d1(1,1);d2(1,1)

[run]

point_query                 = SELECT c FROM sbtest WHERE id=?;1,1,int,1
range_query                 = SELECT c FROM sbtest WHERE id BETWEEN ? AND ?;100,2,int,1,int,2=1+100
range_sum_query             = SELECT SUM(k) FROM sbtest WHERE id BETWEEN ? and ?;1,2,int,1,int,2=1+100
range_orderby_query         = SELECT c FROM sbtest WHERE id between ? and ? ORDER BY c;100,2,int,1,int,2=1+100
range_distinct_query        = SELECT DISTINCT c FROM sbtest  WHERE id BETWEEN ? and ? ORDER BY c;1,2,int,1,int,2=1+100

point_join_query            = SELECT j1.c FROM sbtest as j1, sbtest as j2 WHERE j1.id=j2.id and j1.id=? and j2.id=?;1,2,int,1,int,1
range_join_query            = SELECT j1.c FROM sbtest as j1, sbtest as j2 WHERE j1.id = j2.id and j1.id BETWEEN ? and ? and j2.id BETWEEN ? and ? ;100,4,int,1,int,2=1+100,int,1,int,2=1+100
range_join_sum_query        = SELECT SUM(j1.k) FROM sbtest as j1, sbtest as j2 WHERE j1.id=j2.id and j1.id BETWEEN ? and ? and j2.id BETWEEN ? and ?;1,4,int,1,int,2=1+100,int,1,int,2=1+100
range_join_orderby_query    = SELECT j1.c FROM sbtest as j1, sbtest as j2 WHERE j1.id=j2.id and j1.id between ? and ? and j2.id between ? and ? ORDER BY j1.c;100,4,int,1,int,2=1+100,int,1,int,2=1+100
range_join_distinct_query   = SELECT DISTINCT j1.c FROM sbtest as j1,sbtest as j2 WHERE j1.id=j2.id and j1.id BETWEEN ? and ?  and j2.id BETWEEN ? and ? ORDER BY j1.c;1,4,int,1,int,2=1+100,int,1,int,2=1+100

delete                      = DELETE FROM sbtest WHERE id=?;0,1,int,1
insert                      = INSERT INTO sbtest values(?,0,' ','aaaaaaaaaaffffffffffrrrrrrrrrreeeeeeeeeeyyyyyyyyyy');0,1,int,1
replace                     = REPLACE INTO sbtest values(?,0,' ','aaaaaaaaaaffffffffffrrrrrrrrrreeeeeeeeeeyyyyyyyyyy');0,1,int,1
update_key                  = UPDATE sbtest SET k=k+1 WHERE id=?;0,1,int,1
update_nonkey               = UPDATE sbtest set c=? where id=?;0,2,string,1,int,2

[cleanup]

drop = drop table sbtest
