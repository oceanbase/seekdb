set @@session.ob_query_timeout = 200000000;

use oceanbase;

--disable_query_log
# 1C1G 规格
create resource unit if not exists 1c1g max_cpu 1, memory_size '1G';

# 1C2G 规格
create resource unit if not exists 1c2g max_cpu 1, memory_size '2G';

# 2C2G 规格
create resource unit if not exists 2c2g max_cpu 2, memory_size '2G';

# 2C4G 规格
create resource unit if not exists 2c4g max_cpu 2, memory_size '4G';

--enable_query_log
# obtest doesn't support delimiter, the following stmt can't run.

#  =========================================================================================
#  author: cangming.zl 
#  本文件提供了创建和删除租户相关的procedure，简化操作流程，不再需要了解租户资源规格、资源池概念，提供集成化的接口
#  * 创建租户: 集成创建租户规格、创建资源池、创建租户
#  * 删除租户: 删除租户、删除资源池、删除资源规格
#  使用方法示例：
#     call oceanbase.create_tenant('租户名称', '兼容模式', 'unit_config名字', '资源池zone_list', 'unit数目', 'options', '初始系统变量');
#     call oceanbase.create_tenant('mysql_tenant', 'mysql', '1c1g', '"z1","z2","z3"', 2, 'primary_zone="z1;z2",locality="F@z1, F@z2"', 'ob_query_timeout="100000000"');
#     call oceanbase.drop_tenant();
#  注意事项：
#     * create_xx_tenant()和drop_tenant()需要成对调用，保证删除所有资源
#     * procedure创建在oceanbase库下，需要增加oceanbase前缀
## =================================== create_tenant 模板 ===================================
## create_tenant: 指定规格创建租户
## @param  tenant_name    租户名
## @param  compat_mode    兼容模式，'mysql'
## @param  unit_config    unit规格名，要求规格名已存在，例如：我们预创建了一批规格：1c1g, 1c2g, 2c2g, 2c4g
## @param  zone_list      resouce_pool的zone分布，形如'"z1", "z2", "z3"'
## @param  unit_num       单zone的unit数目
## @param  tenant_options locality、primary_zone等
## @param  sys_variables  初始系统变量
#
#delimiter //;
#drop procedure if exists create_tenant;//
#create procedure create_tenant(tenant_name varchar(64), compat_mode varchar(10), unit_config varchar(64), zone_list varchar(128), unit_num int, tenant_options varchar(1024), sys_variables varchar(1024))
#begin
#  set @pool_name = concat("pool_for_tenant_", tenant_name);
#  set @sql_text = concat("create resource pool if not exists ", @pool_name, "zone_list = (", zone_list ,") unit = '", unit_config, "', unit_num = ", unit_num, ";");
#  prepare stmt from @sql_text;
#  execute stmt;
#
#  if (tenant_options = '') then set @option_str = ''; else set @option_str = ','; end if;
#  if (sys_variables = '') then set @variable_str = ''; else set @variable_str = ','; end if;
#
#  prepare stmt from @sql_text;
#  execute stmt;
#  deallocate prepare stmt;
#end //
#
## =================================== drop_tenant ===================================
## 保证删除掉租户，清理资源
#drop procedure if exists drop_tenant;//
#create procedure drop_tenant(tenant_name varchar(64))
#begin
#  declare recyclebin_value int;
#  select value from oceanbase.CDB_OB_SYS_VARIABLES where name = 'recyclebin' and tenant_id=1 into recyclebin_value;
#  set recyclebin = off;
#
#  set @sql_text = concat("drop tenant if exists ", tenant_name, ";");
#  prepare stmt from @sql_text;
#  execute stmt;
#
#  set @pool_name = concat("pool_for_tenant_", tenant_name);
#  set @sql_text = concat("drop resource pool if exists ", @pool_name, ";");
#  prepare stmt from @sql_text;
#  execute stmt;
#
#  deallocate prepare stmt;
#  set recyclebin = recyclebin_value;
#end //
#
## =================================== drop_tenant_force ===================================
## 快速删除租户，资源可能还保留
#drop procedure if exists drop_tenant_force;//
#create procedure drop_tenant_force(tenant_name varchar(64))
#begin
#  declare recyclebin_value int;
#  select value from oceanbase.CDB_OB_SYS_VARIABLES where name = 'recyclebin' and tenant_id=1 into recyclebin_value;
#  set recyclebin = off;
#
#  set @sql_text = concat("drop tenant if exists ", tenant_name, " force;");
#  prepare stmt from @sql_text;
#  execute stmt;
#
#  set @pool_name = concat("pool_for_tenant_", tenant_name);
#  set @sql_text = concat("drop resource pool if exists ", @pool_name, ";");
#  prepare stmt from @sql_text;
#  execute stmt;
#
#  deallocate prepare stmt;
#  set recyclebin = recyclebin_value;
#end //
#
#delimiter ;//
