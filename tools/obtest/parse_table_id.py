#!/bin/python2

import sys
import re

if __name__ == "__main__":
    if len(sys.argv) != 2:
      print("use cmd table_id")
    else:
      table_id = int(sys.argv[1])
      tenant_id = table_id >> 40
      pure_table_id = table_id - (tenant_id << 40)
      table_name = ""
      prog = re.compile('const uint64_t (\S+) = (\d+); // "(\S+)"')
      with open("../../src/share/inner_table/ob_inner_table_schema_constants.h") as f:
          for line in f:
            m = prog.search(line)
            if m is not None:
              cur_table_id = int(m.group(2))
              if cur_table_id == pure_table_id:
                table_name = m.group(3)
      print("table_id: {} tenant_id: {} pure_table_id: {} table_name={}".format(
        table_id, tenant_id, pure_table_id, table_name))
