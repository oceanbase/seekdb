let $recycle_object_name = deploy_get_value(ob2.select select object_name from oceanbase.__all_recyclebin where original_name = "base_table_to_flashback", object_name, 1);
eval FLASHBACK TABLE $recycle_object_name TO BEFORE DROP;

let $recycle_object_name = deploy_get_value(ob2.select select object_name from oceanbase.__all_recyclebin where original_name = "base_table_to_truncate", object_name, 1);
eval FLASHBACK TABLE $recycle_object_name TO BEFORE DROP RENAME TO base_table_flashbacked;

let $recycle_object_name = deploy_get_value(ob2.select select object_name from oceanbase.__all_recyclebin where original_name = "base_view_to_flashback", object_name, 1);
eval FLASHBACK TABLE $recycle_object_name TO BEFORE DROP;

let $recycle_object_name = deploy_get_value(ob2.select select object_name from oceanbase.__all_recyclebin where original_name = "base_db_to_flashback", object_name, 1);
eval FLASHBACK DATABASE $recycle_object_name TO BEFORE DROP;

let $recycle_object_name = deploy_get_value(ob2.select select object_name from oceanbase.__all_recyclebin where original_name = "base_db_to_flashback_and_rename", object_name, 1);
eval FLASHBACK DATABASE $recycle_object_name TO BEFORE DROP RENAME TO base_db_flashbacked;
