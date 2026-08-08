#!/bin/env python2
'''
/normalize.py a.h a.cpp ...
'''
import sys
import re
import os
import difflib

trans_rule = [
    ('Nomalizer', 'Normalizer'),
    ('  ATOMIC', '  (void)ATOMIC'),
    ('\(void\)ATOMIC_INC', 'ATOMIC_INC'),
    ('\(void\)ATOMIC_DEC', 'ATOMIC_DEC'),
    ('\(void\)ATOMIC_STORE', 'ATOMIC_STORE'),
    # ('([^a-zA-Z_])inited_', r'\1is_inited_'),
    # ('common::OB_SUCCESS != \(ret = ', 'OB_FAIL('),
    # ('OB_SUCCESS != \(ret = ', 'OB_FAIL('),
    # ('NULL == ([a-zA-Z_]+)([^{]+{\n +ret = OB_INVALID_ARGUMENT)', r'OB_ISNULL(\1)\2'),
    # ('!is_inited_', 'IS_NOT_INIT'),
    # ('((?:(?:, )|\())(int64_t|uint64_t|int32_t|uint32_t|int|bool) ([a-zA-Z_]+)(,|\))', r'\1const \2 \3\4'),
    # ('K\(buf\)', 'KP(buf)'),
    # ('K\(buf_\)', 'KP(buf_)'),
    # ('K\(buffer\)', 'KP(buffer)'),
    # ('K\(buffer_\)', 'KP(buffer_)'),
    # ('K\(data\)', 'KP(data)'),
    # ('K\(data_\)', 'KP(data_)'),
]

def join_path(a, b):
    return os.path.normpath(os.path.join(a, b))
def trans_file(fname):
    with open(fname) as f:
        old_text = f.read()
    text = old_text
    for pat, target in trans_rule:
        text = re.sub(pat, target, text)
    if text != old_text:
        print '\n'.join(difflib.unified_diff(old_text.splitlines(), text.splitlines(), join_path('a', fname), join_path('b', fname), lineterm=''))

def help():
    print __doc__

if __name__ == '__main__':
    len(sys.argv) > 1 or help() or sys.exit(1)
    for fname in sys.argv[1:]:
        trans_file(fname)
