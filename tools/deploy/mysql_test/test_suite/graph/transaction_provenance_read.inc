# SELECT only: no CTAS/DDL that could implicitly commit the reader transaction.
# Check raw path bags too, so a missing/deleted document cannot mask a bad path.
--disable_query_log
eval WITH actual AS ($tp_graph), expected AS ($tp_oracle), fallback_paths AS ($tp_fallback)
 SELECT COUNT(*) AS path_bag_mismatches FROM (
 SELECT runtime,binding,tenant,score,endpoint,hops,edges,SUM(n) AS difference FROM (
 SELECT 'native' AS runtime,a.*,1 AS n FROM actual a
 UNION ALL SELECT 'native',e.*,-1 AS n FROM expected e
 UNION ALL SELECT 'fallback',f.*,1 AS n FROM fallback_paths f
 UNION ALL SELECT 'fallback',e.*,-1 AS n FROM expected e
 ) bags GROUP BY runtime,binding,tenant,score,endpoint,hops,edges HAVING SUM(n)<>0) differences;
--echo native_evidence
eval WITH paths AS ($tp_graph) $tp_evidence ORDER BY binding,hops,endpoint,edges,chunk_id;
--echo fallback_evidence
eval WITH paths AS ($tp_fallback) $tp_evidence ORDER BY binding,hops,endpoint,edges,chunk_id;
--enable_query_log
