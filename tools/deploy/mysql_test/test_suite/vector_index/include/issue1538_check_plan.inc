# Run an ANN query on t_issue1538_arith_selectivity with $issue1538_filter
# and print the adaptive vector index scan chosen for it.
--echo # filter: $issue1538_filter
--disable_query_log
--disable_result_log
alter system flush plan cache;
eval select id from t_issue1538_arith_selectivity
where $issue1538_filter
order by l2_distance(embedding, '[0,0,0]') approximate limit 10;
--enable_result_log
select p.operator
from oceanbase.`V$OB_PLAN_CACHE_PLAN_STAT` s
join oceanbase.__all_virtual_sql_plan p on p.plan_id = s.plan_id
where instr(s.statement, 't_issue1538_arith_selectivity') > 0
  and p.operator like 'VECTOR INDEX ADAPTIVE SCAN%';
--enable_query_log
