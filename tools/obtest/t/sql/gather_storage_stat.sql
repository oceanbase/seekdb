--echo
--echo // ****** WHOLE RANGE use index****
connection conn1;
explain select/*+index(t1 primary)*/ * from t1;
connection conn2;
explain select/*+index(t1 primary)*/ * from t1;
connection conn3;
explain select/*+index(t1 primary)*/ * from t1;
connection conn4;
explain select/*+index(t1 primary)*/ * from t1;

--echo
--echo // ****** WHOLE RANGE use index****
connection conn1;
explain select/*+index(t1 idx_c2_c4)*/ * from t1;
connection conn2;
explain select/*+index(t1 idx_c2_c4)*/ * from t1;
connection conn3;
explain select/*+index(t1 idx_c2_c4)*/ * from t1;
connection conn4;
explain select/*+index(t1 idx_c2_c4)*/ * from t1;

--echo
--echo // ****** WHOLE RANGE without hint****
connection conn1;
explain select * from t1;
connection conn2;
explain select * from t1;
connection conn3;
explain select * from t1;
connection conn4;
explain select * from t1;

--echo // **** SKYLINE ONLY ONE PATH ***
--echo //有local副本用存储层信息, 无local副本就default
connection conn1;
explain select c3 from t1 where c3 > 0 and c3 < 10;
connection conn2;
explain select c3 from t1 where c3 > 0 and c3 < 10;
connection conn3;
explain select c3 from t1 where c3 > 0 and c3 < 10;
connection conn4;
explain select c3 from t1 where c3 > 0 and c3 < 10;


--echo
--echo // **** GET ***
connection conn1;
explain select/*+ no_rewrite*/ * from t1 where c1 = 1 or c1 = 2;
connection conn2;
explain select/*+ no_rewrite*/ * from t1 where c1 = 1 or c1 = 2;
connection conn3;
explain select/*+ no_rewrite*/ * from t1 where c1 = 1 or c1 = 2;
connection conn4;
explain select/*+ no_rewrite*/ * from t1 where c1 = 1 or c1 = 2;

--echo
--echo //**** SCAN ***
connection conn1;
explain select * from t1 where (c3 > 0 and c3 < 10);
connection conn2;
explain select * from t1 where (c3 > 0 and c3 < 10);
connection conn3;
explain select * from t1 where (c3 > 0 and c3 < 10);
connection conn4;
explain select * from t1 where (c3 > 0 and c3 < 10);

--echo
--echo //**** GET AND SCAN ****
connection conn1;
explain select/*+ no_rewrite*/ * from t1 where (c1 in (11, 12, 13) or (c1 > 0 and c1 < 10)) and c3 > 10;
connection conn2;
explain select/*+ no_rewrite*/ * from t1 where (c1 in (11, 12, 13) or (c1 > 0 and c1 < 10)) and c3 > 10;
connection conn3;
explain select/*+ no_rewrite*/ * from t1 where (c1 in (11, 12, 13) or (c1 > 0 and c1 < 10)) and c3 > 10;
connection conn4;
explain select/*+ no_rewrite*/ * from t1 where (c1 in (11, 12, 13) or (c1 > 0 and c1 < 10)) and c3 > 10;

--echo // ***** MULTI SCAN ****
connection conn1;
explain select * from t1 where c2 in (10, 20, 30, 40, 50, 60, 70);
connection conn2;
explain select * from t1 where c2 in (10, 20, 30, 40, 50, 60, 70);
connection conn3;
explain select * from t1 where c2 in (10, 20, 30, 40, 50, 60, 70);
connection conn4;
explain select * from t1 where c2 in (10, 20, 30, 40, 50, 60, 70);
