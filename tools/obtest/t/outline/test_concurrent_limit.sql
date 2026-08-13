--error 5268
select  c2,c1 from t1 where c1 = 2 and c2 = "2";
--error 5268
select  c2,c1 from t1 where c1 = 2 and c2 = "1";
--error 5268
select  c2,c1 from t1 where c1 = 2 and c2 = 1;
--error 5268
select  c2,c1 from t1 where c1 = 2 and c2 = true;

select  c2,c1 from t1 where c1 = 1 and c2 = "2";
select  c2,c1 from t1 where c1 = 1 and c2 = "1";
select  c2,c1 from t1 where c1 = 1 and c2 = 1;
select  c2,c1 from t1 where c1 = 1 and c2 = true;
