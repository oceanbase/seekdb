eval select hit_count, outline_sql from oceanbase.V\$OB_PLAN_CACHE_PLAN_STAT as a join oceanbase.DBA_OB_OUTLINES as b on a.outline_id = b.outline_id where b.outline_name = "$outline_name";
--disable_result_log
--disable_query_log
eval select @plan_id := (select plan_id from oceanbase.V\$OB_PLAN_CACHE_PLAN_STAT as a, oceanbase.DBA_OB_OUTLINES as b where a.outline_id = b.outline_id and b.outline_name = "$outline_name");
let $plan_id = `select @plan_id`;
--enable_result_log
eval select name, operator from oceanbase.V\$OB_PLAN_CACHE_PLAN_EXPLAIN where tenant_id = $tenant_id and plan_id = $plan_id;
--enable_query_log
