--disable_query_log
--disable_result_log
let $use_spb_bk = query_get_value(select variable_value from information_schema.session_variables where variable_name = 'optimizer_use_sql_plan_baselines', variable_value, 1);
let $cap_spb_bk = query_get_value(select variable_value from information_schema.session_variables where variable_name = 'optimizer_capture_sql_plan_baselines', variable_value, 1);
set global optimizer_use_sql_plan_baselines = 'OFF';
set global optimizer_capture_sql_plan_baselines = 'OFF';
set session optimizer_use_sql_plan_baselines = 'OFF';
set session optimizer_capture_sql_plan_baselines = 'OFF';
--enable_result_log
--enable_query_log
