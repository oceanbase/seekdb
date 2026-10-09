#!/usr/bin/env python3
from pathlib import Path
import argparse
import re
p=argparse.ArgumentParser()
p.add_argument('action',choices=('enable','disable'))
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
target=root/'src/storage/tx_storage/ob_empty_shell_task.cpp'
text=re.sub(r'^[ \t]*// LOCAL_EMPTY_SHELL_HORIZON_BEGIN\n.*?^[ \t]*// LOCAL_EMPTY_SHELL_HORIZON_END\n','',target.read_text(),flags=re.M|re.S)
def block(code):return '// LOCAL_EMPTY_SHELL_HORIZON_BEGIN\n'+code+'// LOCAL_EMPTY_SHELL_HORIZON_END\n'
if a.action=='enable':
    anchor='int ObTabletEmptyShellHandler::get_empty_shell_tablet_ids(\n'
    assert text.count(anchor)==1
    helper='''static void probe_empty_shell_horizon(share::SCN &snapshot)
{
  const char *path = getenv("SEEKDB_EMPTY_SHELL_READ_HORIZON");
  if (path) {
    FILE *file = fopen(path, "r");
    if (file) {
      long selected = 0;
      if (fscanf(file, "%ld", &selected) == 1 && selected > 0) {
        (void)snapshot.convert_for_tx(selected);
      }
      fclose(file);
    }
  }
}
'''
    text=text.replace(anchor,block(helper)+anchor)
    anchor='  } else if (!new_read_snapshot.is_valid_and_not_min() || new_read_snapshot.is_max()) {\n'
    assert text.count(anchor)==1
    text=text.replace(anchor,block('  } else if (FALSE_IT(probe_empty_shell_horizon(new_read_snapshot))) {\n')+anchor)
if text!=target.read_text():target.write_text(text)
print('empty shell horizon hook',a.action)
