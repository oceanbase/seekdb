#!/bin/bash

function run_single_case() {
  CASE=$1
  CMD="./mytest $CASE"
  LOG_PATH="r/"
  if [ -f $case ]; then
    if [[ $CASE == "./t/"* ]]; then
      LOG_PATH=`echo $CASE|sed "s#^./t/#log/#g"|sed "s#\.test##g"`
    else
      LOG_PATH=`echo $CASE|sed "s#^t/#log/#g"|sed "s#\.test##g"`
    fi
  fi

  echo "====== `date`: start run-obtest-until-fail.sh $CASE"
  echo "COUNT_LIMIT=$COUNT_LIMIT"
  echo "LOG_PATH=$LOG_PATH"
  echo "CMD=$CMD"
  COUNT=0
  while [ True ]; do
    if [ $COUNT -eq $COUNT_LIMIT ]; then 
      echo "====== Congratulations on passing $COUNT times: $CASE"
      break
    fi
    ((COUNT++))
    MSG="`date`: run until $COUNT/$COUNT_LIMIT: $CASE"
    echo $MSG
    $CMD  >> /dev/null 2>&1
    if [ $? -ne 0 ]; then
      echo "====== Failed at $COUNT/$COUNT_LIMIT during run test $CASE"
      break
    fi
  done
}



COUNT_LIMIT=10
CASE_LIST=$1
LOG_PATH=

if [ $# -eq 0 ]; then
  echo "./run-obtest-until-fail.sh count_limit case1,case2"
  echo "./run-obtest-until-fail.sh case1,case2 # default count limit is 10"
  exit 1
fi

if [ $# -eq 2 ]; then
  COUNT_LIMIT=$1
  CASE_LIST=$2
fi

echo "COUNT_LIMIT=$COUNT_LIMIT"
echo "CASE_LIST=$CASE_LIST"

IFS=',' read -r -a CASE_ARRAY <<< "$CASE_LIST"

for CASE in "${CASE_ARRAY[@]}"; do
  run_single_case $CASE
done

