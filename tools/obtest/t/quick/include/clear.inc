#############################################
#owner : @wanhong.wwh
#owner group: rs
#description: quick test
# 测试点：清理环境
#############################################

####### 删除租户，顺便验证相关依赖的视图
connection conn_sys;
set recyclebin = 'OFF';