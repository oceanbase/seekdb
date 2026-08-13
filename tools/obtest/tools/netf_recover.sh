#!/bin/bash
sleep $1 && sudo tc qdisc del dev $2 root > /dev/null 2>&1 &
