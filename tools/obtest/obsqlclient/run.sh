#!/usr/bin/env bash
export OB_SQL_CONFIG=/home/yiming.czw/obtest/obsqlclient/libobsql.conf      
export LD_PRELOAD=/home/yiming.czw/obtest/obsqlclient/libobsql.so.0.0.0 
./obsqltest -t $1

