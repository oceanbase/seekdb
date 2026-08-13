#!/bin/bash

# 确保提供了两个文件名
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <fileA> <fileB>"
    exit 1
fi

# 将参数分配给变量以增加可读性
fileA=$1
fileB=$2

# 使用grep -F -v -x -f
# -F: 固定字符串匹配，不解释任何正则语法
# -v: 反向匹配，选出不匹配的行
# -x: 只有整行匹配时才算匹配成功
# -f: 从文件中获取匹配模式
grep -F -v -x -f "$fileB" "$fileA"
