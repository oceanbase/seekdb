# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
"""Small raw TSV client for the AI integration runner; requires PyMySQL."""
import argparse
import os
import sys


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('-h', dest='host', default='127.0.0.1')
    parser.add_argument('-P', dest='port', type=int, default=2881)
    parser.add_argument('-u', dest='user', default='root')
    parser.add_argument('-D', dest='database', default='test')
    parser.add_argument('-N', action='store_true')
    parser.add_argument('-B', action='store_true')
    parser.add_argument('--raw', action='store_true')
    parser.add_argument('-e', dest='statement', required=True)
    args = parser.parse_args()
    import pymysql
    sys.stdout.reconfigure(encoding='utf-8')
    with pymysql.connect(host=args.host, port=args.port, user=args.user,
                         database=args.database, password=os.getenv('MYSQL_PWD', ''),
                         charset='utf8mb4', autocommit=True,
                         connect_timeout=10, read_timeout=180) as connection:
        with connection.cursor() as cursor:
            cursor.execute(args.statement)
            while True:
                if cursor.description:
                    for row in cursor.fetchall():
                        print('\t'.join('NULL' if value is None else str(value) for value in row))
                if not cursor.nextset():
                    break


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
