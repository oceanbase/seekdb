
#########################
#外部变量: $cpu,$unitnum,$tenantnum
###################
if ($cpu == null)
{
	let $cpu=1;
}

if ($unitnum == null)
{
	let $unitnum=1;
}

if ($tenantnum == null)
{
	let $tenantnum=1;
}

let $unitname=unit;
let $poolname=pool;
let $tenantname=tt;

let $z_num=deploy_get_value(ob1.z_num);

###########
# 创建默认单元，租户
##################
ob1.mysql CREATE RESOURCE UNIT $unitname max_cpu $cpu, MEMORY_SIZE 1073741824;

while ($tenantnum > 0)
{
				let $znum = $z_num;
				while($znum >= 1)
				{
					let $pool = $poolname$tenantnum$znum;
					ob1.mysql CREATE RESOURCE POOL $pool UNIT='$unitname', UNIT_NUM =$unitnum , ZONE='z$znum';
					
					# 获取pool列表
					if ($znum == $z_num)
					{
						let $poollist = '$pool';
					}
					if ($znum != $z_num)
					{
						let $poollist = $poollist,'$pool';
					}
					
					dec $znum;
				}
				ob1.mysql CREATE TENANT $tenantname$tenantnum replica_num = $z_num, primary_zone='z1', resource_pool_list=($poollist);
				
				--disable_warnings
				--disable_query_log
				--disable_result_log
				sleep 5;
				--error 0,1064
				ob1.mysql alter tenant $tenantname$tenantnum set variables ob_tcp_invited_nodes='%';
				--enable_warnings
				--enable_query_log
				--enable_result_log

				dec $tenantnum;
}


################
# 创建数据表
# 创建规则：创建表的数量与每个zone的最大单元数一致
##############
#ob1.set_tenant $tenantname;
#while($maxObsNum>0)
#{
#	let $tablename=t$maxObsNum;
#	source sql/init_sql.sql;
#	dec $maxObsNum;
#}


