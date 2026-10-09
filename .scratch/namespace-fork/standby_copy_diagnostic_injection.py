#!/usr/bin/env python3
import argparse
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('action',choices=('enable','disable'));args=parser.parse_args()
local=Path(__file__).resolve().parent; root=local.parent.parent
paths=('src/standby/restore/ob_sstable_copy_finish_task.cpp','src/standby/restore/ob_standby_sstable_copier.cpp')
for name in paths:
 p=root/name;backup=local/(p.name+'.copy-diagnostic-original')
 if args.action=='enable':
  if backup.exists(): raise RuntimeError('already enabled '+name)
  original=p.read_text();backup.write_text(original)
  p.write_text(original.replace('LOG_WARN(', 'LOG_INFO('))
 else:
  if backup.exists(): p.write_text(backup.read_text());backup.unlink()
