from pathlib import Path
h=Path('src/standby/restore/ob_standby_restore_reader.h'); header=h.read_text()
p=Path('src/standby/restore/ob_standby_restore_reader.cpp'); text=p.read_text()
for cls in ('ObCopyMacroBlockObProducer','ObCopySSTableMacroObProducer','ObCopySSTableMacroRangeObProducer'):
 start=header.index('class '+cls+'\n') if cls!='ObCopySSTableMacroRangeObProducer' else header.index('class '+cls+' :')
 a=header.index('  int init(',start); b=header.index(');',a)
 header=header[:b]+',\n      const ObTabletHandle &handle'+header[b:]
 a=text.index('int '+cls+'::init('); b=text.index('\nint ',a+1)
 f=text[a:b]
 marker='const int64_t io_timeout_ms)' if cls=='ObCopyMacroBlockObProducer' else 'const int64_t macro_range_max_marco_count)'
 assert f.count(marker)==1
 f=f.replace(marker,marker[:-1]+',\n    const ObTabletHandle &handle)')
 f=f.replace('  ObLSService *ls_service = nullptr;\n','').replace('  ObLS *ls = nullptr;\n','')
 f=f.replace('} else if (!ls_id.is_valid()', '} else if (!handle.is_valid() || !ls_id.is_valid()',1)
 key='table_key.get_tablet_id()' if cls=='ObCopyMacroBlockObProducer' else 'tablet_id'
 a1=f.index('  } else if (OB_ISNULL(ls_service =')
 b1=f.index('\n  } else',f.index('LOG_WARN("failed to get tablet",',a1))
 f=f[:a1]+f'''  }} else if (handle.get_obj()->get_tablet_meta().tablet_id_ != {key}) {{
    ret = OB_INVALID_ARGUMENT;
  }} else if (FALSE_IT(tablet_handle_ = handle)) {{'''+f[b1:]
 text=text[:a]+f+text[b:]
# SSTable metadata producer receives the same captured handle.
start=header.index('class ObCopySSTableInfoObProducer');end=header.index('\n};',start)
part=header[start:end];assert part.count('ObLS *ls')==1
header=header[:start]+part.replace('ObLS *ls','const ObTabletHandle &handle')+header[end:]
a=text.index('int ObCopySSTableInfoObProducer::init(');b=text.index('\nint ',a+1)
f=text[a:b].replace('    ObLS *ls)','    const ObTabletHandle &handle)')
f=f.replace('|| OB_ISNULL(ls)', '|| !handle.is_valid()').replace('K(tablet_sstable_info), KP(ls)', 'K(tablet_sstable_info)')
a1=f.index('  // Physical restore must include');b1=f.index('  } else if (OB_ISNULL(tablet = tablet_handle_.get_obj()))',a1)
f=f[:a1]+'''  } else if (handle.get_obj()->get_tablet_meta().tablet_id_ != tablet_sstable_info.tablet_id_) {
    ret = OB_INVALID_ARGUMENT;
  } else if (FALSE_IT(tablet_handle_ = handle)) {
'''+f[b1:]
text=text[:a]+f+text[b:]
# Tablet-info producer: all handles have already been resolved in the view.
a=header.index('class ObCopyTabletInfoObProducer');b=header.index('\n};',a)
f=header[a:b].replace('    const share::ObLSID &ls_id,\n    const common::ObIArray<common::ObTabletID> &tablet_id_array','    const common::ObIArray<ObTabletHandle> &tablets')
f=f.replace('  ObArray<common::ObTabletID> tablet_id_array_;','  ObArray<ObTabletHandle> tablets_;').replace('  ObLS *ls_;\n','')
header=header[:a]+f+header[b:]
a=text.index('ObCopyTabletInfoObProducer::ObCopyTabletInfoObProducer()');b=text.index('ObCopyTabletsSSTableInfoObProducer::',a)
f=text[a:b]
f=f.replace('    tablet_id_array_(),\n    tablet_index_(0),\n    ls_(nullptr)','    tablets_(),\n    tablet_index_(0)')
x=f.index('int ObCopyTabletInfoObProducer::init(');y=f.index('int ObCopyTabletInfoObProducer::get_next_tablet_info',x)
f=f[:x]+'''int ObCopyTabletInfoObProducer::init(const common::ObIArray<ObTabletHandle> &tablets)
{
  int ret = OB_SUCCESS;
  if (is_inited_) { ret = OB_INIT_TWICE; }
  else if (tablets.empty()) { ret = OB_INVALID_ARGUMENT; }
  else if (OB_FAIL(tablets_.assign(tablets))) {
  } else { is_inited_ = true; }
  return ret;
}

'''+f[y:]
f=f.replace('  ObLS *ls = nullptr;\n','').replace('  ObTabletHandle tablet_handle;\n','').replace('tablet_id_array_.count()','tablets_.count()')
f=f.replace('    const ObTabletID &tablet_id = tablet_id_array_.at(tablet_index_);','    const ObTabletHandle &tablet_handle = tablets_.at(tablet_index_);\n    const ObTabletID &tablet_id = tablet_handle.get_obj()->get_tablet_meta().tablet_id_;')
x=f.index('    if (OB_ISNULL(ls = ls_))');y=f.index('    } else if (OB_ISNULL(tablet = tablet_handle.get_obj()))',x)
f=f[:x]+f[y:].replace('    } else if (OB_ISNULL(tablet = tablet_handle.get_obj()))','    if (OB_ISNULL(tablet = tablet_handle.get_obj()))',1)
text=text[:a]+f+text[b:]
# ObCopySSTableMacroObProducer no longer resolves an LS.
a=header.index('class ObCopySSTableMacroObProducer');b=header.index('\n};',a)
header=header[:a]+header[a:b].replace('  ObLS *ls_;\n','')+header[b:]
a=text.index('ObCopySSTableMacroObProducer::ObCopySSTableMacroObProducer()');b=text.index('int ObCopySSTableMacroObProducer::init(',a)
text=text[:a]+text[a:b].replace('    ls_(nullptr),\n','')+text[b:]
h.write_text(header);p.write_text(text)
