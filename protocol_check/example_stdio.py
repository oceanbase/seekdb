#!/usr/bin/env python3
"""Communication skeleton only: connect your own database to execute_sql()."""
import json
import sys

from _transport import strict_json


class DatabaseError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def execute_sql(sql, search_mode):
    # Replace with the real engine; return columns/rows or affected_rows.
    raise DatabaseError('NOT_IMPLEMENTED', 'Connect your database engine here')


def recover_database():
    # Open /data, replay recovery, and initialize the engine before ready.
    pass


def close_database():
    # Roll back outstanding transactions, persist committed state, close files.
    pass


def emit(message):
    print(json.dumps(message, ensure_ascii=False, allow_nan=False), flush=True)


def main():
    recover_database()
    emit({'type': 'ready'})
    for line in sys.stdin:
        request = strict_json(line)
        ident = request['id']
        try:
            if request['op'] == 'shutdown':
                close_database()
                emit({'id': ident, 'ok': True})
                return
            if request['op'] != 'execute':
                raise DatabaseError('UNSUPPORTED_OP', 'Unsupported operation')
            result = execute_sql(request['sql'], request.get('search_mode', 'exact'))
            emit({**result, 'id': ident, 'ok': True})
        except DatabaseError as exc:
            emit({'id': ident, 'ok': False, 'error': {'code': exc.code, 'message': str(exc)}})
    close_database()


if __name__ == '__main__':
    main()
