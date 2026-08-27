#usage: $version;

let $cnt = 360;
while ($cnt > 0)
{
  let $merge = deploy_get_value(ob1.select select count(*) c from oceanbase.__all_zone where name ="last_merged_version" and value = $version and zone = '', c, 1);
  if ($merge == 1)
  {
    let $cnt = 0;
  }
  #在合并的zone不能有主存在；
  let $leader = deploy_get_value(ob1.select select  /*+ READ_CONSISTENCY(WEAK) */ count(*) c from oceanbase.__all_zone, oceanbase.__all_virtual_meta_table where role = 1 and oceanbase.__all_zone.zone = oceanbase.__all_virtual_meta_table.zone  and oceanbase.__all_zone.name = "merge_status" and oceanbase.__all_zone.info="MERGING", c, 1);
  if ($leader > 1)
  {
    echo 'failed';
  }
  #主不能切到其他region
  let $leader = deploy_get_value(ob1.select select /*+ READ_CONSISTENCY(WEAK) */ count(*) c from oceanbase.__all_virtual_meta_table where role = 1 and zone != 'z1' and zone != 'z2' and zone != 'z3', c, 1);
  if ($leader > 0)
  {
    echo 'failed';
  }
  sleep 1;
  dec $cnt;
}
