#! /usr/bin
#if [ ! -d collected_log ];then
#   echo "mkdir collected_log"
#   mkdir   collected_log
#fi
echo "remove old collect_log.back except last two backup "
ls -dt collected_log.*

# 保留最后2份
ls -dt collected_log.* | sed -n '10,$p' | xargs -I {} rm -rf {}

log_dir="collected_log"
if [ -d $log_dir ];then
   echo "backup collected_log"
   backup_dir=`stat -c %y $log_dir | awk -F"." '{ print $1 }' | sed -e"s/\\-//g" -e"s/\\://g" -e"s/[ ]//g"`
   mkdir -p collected_log.${backup_dir}
   echo "observer logs saved to collected_log.${backup_dir}"
   mv $log_dir collected_log.${backup_dir}
fi
