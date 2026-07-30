#!/usr/bin/python
import sys
import re
import os
import getopt
import mysql.connector
from mysql.connector import errorcode
import argparse

class TimeZoneInfoImporter:
    def get_args(self):
        parser = argparse.ArgumentParser(conflict_handler='resolve')
        parser.add_argument("-h", "--host", help="Connect to host", required=True)
        parser.add_argument("-P", "--port", type=int, help="Port number to use for connection", required=True)
        parser.add_argument("-p", "--password", default='', help="Password of root user")
        parser.add_argument("-f", "--file", help="The script generate from MySQL mysql_tzinfo_to_sql", required=True)
        args = parser.parse_args()
        self.host=args.host
        self.port=args.port
        self.pwd=args.password
        self.file_name=args.file

    def generate_sql(self):
        self.sql_list = []
        self.tz_version_sql_list = []
        self.expect_count = [0, 0, 0, 0]
        retained_tables = (
            'time_zone_name',
            'time_zone_transition',
            'time_zone_transition_type',
        )
        replace_count_str0 = 'time_zone count:'
        replace_count_str1 = 'time_zone_name count:'
        replace_count_str2 = 'time_zone_transition count:'
        replace_count_str3 = 'time_zone_transition_type count:'
        with open(self.file_name) as f_read:
            sql = ""
            for line in f_read:
                if re.search('__all_sys_stat', line, re.IGNORECASE):
                    if re.search(r'(?:INSERT|REPLACE)\s+INTO\s+oceanbase\.__all_sys_stat',
                                 line, re.IGNORECASE):
                        version_match = re.search(
                            r"['\"]current_timezone_version['\"]\s*,\s*([0-9]+)\s*,",
                            line, re.IGNORECASE)
                        if version_match is None:
                            raise ValueError(
                                'failed to parse current_timezone_version SQL: {0}'.format(line))
                        self.tz_version_sql_list.append(
                            "REPLACE INTO oceanbase.__all_sys_stat"
                            "(data_type, name, value, info) VALUES"
                            "(5, 'current_timezone_version', {0}, "
                            "'current time zone version');\n".format(
                                version_match.group(1)))
                    else:
                        self.tz_version_sql_list.append(line)
                elif re.search('count:', line, re.IGNORECASE):
                    if re.search(replace_count_str3, line, re.IGNORECASE):
                        self.expect_count[3] = int(line.replace(replace_count_str3, ''))
                    elif re.search(replace_count_str2, line, re.IGNORECASE):
                        self.expect_count[2] = int(line.replace(replace_count_str2, ''))
                    elif re.search(replace_count_str1, line, re.IGNORECASE):
                        self.expect_count[1] = int(line.replace(replace_count_str1, ''))
                    elif re.search(replace_count_str0, line, re.IGNORECASE):
                        self.expect_count[0] = int(line.replace(replace_count_str0, ''))
                else:
                    # The MySQL time_zone table only stores the leap-second flag,
                    # which SeekDB does not use. Ignore that table while importing
                    # the three tables needed for named-zone conversion.
                    if re.search(r'(TRUNCATE\s+TABLE|(?:INSERT|REPLACE)\s+INTO)\s+time_zone\b',
                                 line, re.IGNORECASE):
                        new_line = ''
                    elif re.search(r'ALTER\s+TABLE\s+time_zone_transition\b',
                                   line, re.IGNORECASE):
                        new_line = ''
                    else:
                        new_line = line
                        for table_name in retained_tables:
                            new_line = re.sub(
                                r'(TRUNCATE\s+TABLE\s+)' + table_name + r'\b',
                                r'\1oceanbase.__all_' + table_name,
                                new_line,
                                flags=re.IGNORECASE)
                            new_line = re.sub(
                                r'(INTO\s+)' + table_name + r'\b',
                                r'\1oceanbase.__all_' + table_name,
                                new_line,
                                flags=re.IGNORECASE)
                    new_line = new_line.replace('tid', "0")
                    sql += new_line
                    if ";" in new_line:
                        self.sql_list.append(sql)
                        sql = ""

    def connect_server(self):
        self.conn = mysql.connector.connect(user='root', password=self.pwd, host=self.host, port=self.port, database='mysql')
        self.cur = self.conn.cursor(buffered=True)
        print ("INFO : sucess to connect server {0}:{1}".format(self.host, self.port))
    def execute_sql(self):
        try:
            for sql in self.sql_list:
                self.cur.execute(sql);
                print ("INFO : execute sql -- {0}".format(sql))
        except mysql.connector.Error as err:
            print("ERROR : " + sql)
            print(err)
            print("ERROR : fail to import time zone info")
            raise
        else:
            print("INFO : success to import time zone info")

    def execute_check_sql(self, table_name, idx):
        self.cur.execute("select count(*) from {0}".format(table_name))
        result = self.cur.fetchone()
        self.result_count[idx] = result[0]
        print ("INFO : {0} record count -- {1}, expect count -- {2}".format(table_name, result[0], self.expect_count[idx]))

    def check_result(self):
        self.result_count = [0, 0, 0, 0]
        self.execute_check_sql("oceanbase.__all_time_zone_name", 1)
        self.execute_check_sql("oceanbase.__all_time_zone_transition", 2)
        self.execute_check_sql("oceanbase.__all_time_zone_transition_type", 3)
        if self.expect_count[1] == self.result_count[1] \
            and self.expect_count[2] == self.result_count[2] \
            and self.expect_count[3] == self.result_count[3]:
            try:
                for sql in self.tz_version_sql_list:
                    self.cur.execute(sql)
                    print ("INFO : execute sql -- {0}".format(sql))
            except mysql.connector.Error as err:
                print("ERROR : " + sql)
                print(err)
                print("ERROR : fail to insert time zone version")
                raise
            else:
                print("INFO : success to insert time zone version")
        else:
            raise RuntimeError("time zone row counts do not match the import manifest")

def main():
    tz_info_importer = TimeZoneInfoImporter()
    tz_info_importer.get_args()
    try:
        tz_info_importer.connect_server()
        tz_info_importer.generate_sql()
        tz_info_importer.execute_sql()
        tz_info_importer.check_result()
    except Exception as err:
        print("ERROR: {0}".format(err))
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
