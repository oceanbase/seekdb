#!/bin/env python
__author__ = 'dongyun.zdy'
import subprocess as sp
import os
import sys

def remove_schema():
    global schema_file
    if os.path.exists(schema_file):
        os.remove(schema_file)

def write_schema(s):
    global schema_file
    of = open(schema_file, 'w')
    of.write(s)
    of.close()


def make_seq(t, cnt):
    types = [t]
    types *= cnt
    return types

schema_file = 'scan.schema'
def make_schema(types):
    global schema_file
    remove_schema()
    col_id = 0
    s = "create table t1 ("
    for t in types:
        s += "c%d %s, " % (col_id, t)
        col_id += 1
    s = s[:-2]
    s += ', primary key (c1))'
    print s
    write_schema(s)


types = {'bi':'bigint', 'vc32':'varchar(32)', 'vc128':'varchar(128)', 'db':'double', 'ts':'timestamp', 'nb':'number'}


types_to_test = ['bi', 'vc32', 'db', 'ts', 'nb']
col_type_repeat_times = 10
table_width_factors = [3, 2, 1]





def run_cmd(cmd):
    # print cmd
    res = ''
    p = sp.Popen(cmd, shell=True, stdout=sp.PIPE, stderr=sp.STDOUT)
    while True:
        line = p.stdout.readline()
        res += line
        if line:
            #print line.strip()
            sys.stdout.flush()
        else:
            break
    p.wait()
    return res

outfile_name = 'scan'

cmd_form = './storage_perf_cost -G -C 1 -s scan.schema -T 10 -r 1000 -Y S -E -I'.split()

rows_min = 1
rows_max = 1000001

row_counts = sorted(list(set(range(2, 1002, 100) + range(1002, 10002, 1000) + range(10002, 100002, 10000) + range(100002, 1000003, 100000))))
col_counts = [1,10,20,40,50]
modes = ['W', 'NORMAL']
mode_ids = {'W':1, 'NORMAL':2}

total_count = len(row_counts) * len(col_counts) * len(modes) * len(table_width_factors)
count = 0


for table_width_factor in table_width_factors:
    seq = []
    for t in types_to_test:
        seq.extend([types[t] for i in range(col_type_repeat_times)])
    make_schema(seq * table_width_factor)
    run_cmd('./storage_perf_cost -G -C 1 -s scan.schema -T 1 -r %d -Y S -R' % (2 * rows_max))

    for mode in modes:

        outfile = outfile_name + '.' + mode + '.w%d.res' % table_width_factor
        if os.path.exists(outfile):
            os.remove(outfile)

        for col_count in col_counts:
            for rc in row_counts:

                prop = '%d,%d,%d,' % (rc, col_count, table_width_factor)

                count += 1
                cmd_form[9] = str(rc)
                cmd_form[3] = str(col_count)
                if mode == 'NORMAL':
                    cmd_form[12] = ''
                else:
                    cmd_form[12] = '-' + mode
                cmd = ' '.join(cmd_form)
                print '%d / %d : ' % (count, total_count) + cmd
                run_cmd('echo "# %s" >> ' % cmd + outfile)
                cmd_res = filter(lambda x : x.strip() != '', run_cmd(cmd).strip().split('\n\n'))
                if len(cmd_res) == 0:
                    run_cmd('echo "# error" >> ' + outfile)
                for runinfo in cmd_res:
                    lines = runinfo.splitlines()
                    resline = prop + lines[0]
                    print resline
                    run_cmd('echo "%s" >> ' % (resline) + outfile)
                    for statline in lines[1:]:
                        run_cmd('echo "# %s" >> ' % statline + outfile)
