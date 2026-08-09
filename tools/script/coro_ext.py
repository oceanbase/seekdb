# (C) 2018 Alibaba Group Holding Limited.
# Authors:

import gdb
import gdb.printing
import gdb.types
import gdb.unwinder
import traceback
import re

# basic type mapping within oceanbase coro source 
sched_type = gdb.lookup_type("oceanbase::lib::CoSched")
base_sched_type = gdb.lookup_type("oceanbase::lib::CoBaseSched")
worker_type = gdb.lookup_type("oceanbase::lib::CoBaseSched::Worker")
routine_type = gdb.lookup_type("oceanbase::lib::CoRoutine")
worker_dlink_type = gdb.lookup_type("oceanbase::common::ObDLinkBase<oceanbase::lib::CoBaseSched::Worker>")
int_type = gdb.lookup_type('int64_t')

def g_exec(cmd, to_string = False):
  gdb.execute(cmd, to_string = to_string)

def g_pe(cmd):
  return gdb.parse_and_eval(cmd)

# current coro, it will updated when coro switched 
cur_coro = None

def set_cur_coro(coro):
  global cur_coro
  cur_coro = coro


def get_cur_coro():
  global cur_coro
  return cur_coro


class CurThread:
  """static util class for fetch current thread info"""
  @staticmethod
  def thread_id():
    g_th = gdb.selected_thread()
    if not g_th:
      raise gdb.GdbError('no coros in process')
    return Thread(g_th).thread_id()

  @staticmethod
  def gdb_thread():
    return gdb.selected_thread() 


class Thread:
  """gdb thread wrapper"""
  def __init__(self, gdb_thread):
    self.gdb_thread = gdb_thread 
  def thread_id(self):
    return self.gdb_thread.ptid[1]
  def thread_num(self):
    return self.gdb_thread.global_num


class Coro:
  """coro wrapper, mark some basic coro info"""
  def __init__(self, thread, coro_id, routine, is_active, is_sched):
    self.thread = thread
    self.coro_id = coro_id
    self.routine = routine 
    self.is_active = is_active 
    self.is_sched = is_sched

  def __str__(self):
    return 'thread_num: {0}, thread_id:{1}, coro_id:{2}, routine:{3}, is_active:{4}, \
        is_sched:{5}'.format(self.thread.thread_num(), self.thread.thread_id(), \
            self.coro_id, self.routine, self.is_active, self.is_sched)

  def run_status(self):
    rs = self.routine['rs_'] # oceanbase::lib::CoRoutine::RunStatus::BORN
    return str(rs).split('::')[-1]


def __threads():
  """all threads generator"""
  return gdb.selected_inferior().threads()


def __coros(thread):
  """all coros generator within a custom thread"""
  thread.switch()
  sched = None
  sched = gdb.parse_and_eval("""'{0}::instance_'""".format(sched_type.name))
  if str(sched) == '0x0': return
  # sched is main routine of thread
  routine = sched.cast(routine_type.pointer())
  is_active = sched['active_routine_'] == routine
  yield Coro(Thread(thread), int(routine['id_']), routine, is_active, True)
  sched = sched.cast(base_sched_type.pointer())
  header = sched['routines_']['header_']
  worker = header
  while 1:
    worker = worker['next_']
    worker_addr = worker.cast(worker_type.pointer()).cast(worker_dlink_type.pointer())
    if worker_addr == header.address : break
    routine = worker.cast(routine_type.pointer())
    is_active = sched['active_routine_'] == routine
    yield Coro(Thread(thread), int(routine['id_']), routine, is_active, False)


def need_restore(func):
  """a decorator which will backup current coro before invoke func and restore it after"""
  def wrapper(*args):
    # backup
    orig_thread = CurThread.gdb_thread() 
    orig_coro = get_cur_coro()

    # process 
    func(*args)

    # restore
    cur_coro = orig_coro
    gdb.invalidate_cached_frames()
    CoroUnwinder.set_coro(cur_coro)
    orig_thread.switch()

  return wrapper


def coro_apply_impl(thread_id, coro_id, func):
  """invoke func on all coros
    @thread_id = -1, @coro = -1: all coros of all threads
    @thread_id = -1, @coro = xxx: just coro xxx
    @thread_id = xxx, @coro = -1: all coros of thread xxx
    @thread_id = xxx, @coro = xxx: just coro xxx 
  """
  has_coro = False
  for thread in __threads():
    if thread_id in (-1, thread.ptid[1]): 
      for coro in __coros(thread):
        if coro_id in (-1, coro.coro_id):
          has_coro = True 
          func(coro)
          if coro_id != -1: return
  if not has_coro:
    if thread_id == -1 and coro_id == -1:
      raise gdb.GdbError('no coros in process')
    if coro_id != -1:
      raise gdb.GdbError('coro not exist, coro_id: {0}'.format(coro_id))
    elif thread_id != -1:
      print 'no coros in thread, thread_id: {0}'.format(thread_id)
          
      
@need_restore
def coro_apply(thread_id, coro_id, func):
  return coro_apply_impl(thread_id, coro_id, func)


def get_ptr(base, inc, is_add = True):
  """compute new ptr gdb value by base gdb ptr value and inc value"""
  left, right = base.cast(int_type),  gdb.Value(inc).cast(int_type)
  val = (left + right) if is_add else (left - right)
  return val.cast(int_type.pointer())


class Regs:
  """register value wrapper"""
  def __init__(self, r12, r13, r14, r15, rbx, rbp, rip, rsp):
    self.r12 = r12
    self.r13 = r13
    self.r14 = r14
    self.r15 = r15
    self.rbx = rbx
    self.rbp = rbp
    self.rip = rip
    self.rsp = rsp

  def __str__(self):
    return ','.join('{' + str(_) + '}' for _ in range(len(8))).format( \
        self.r12, self.r13, self.r14, self.r15, self.rbx, self.rbp, self.rip, self.rsp)


def get_regs_from_fctx(fctx):
  """parse register value from fctx which stored in routine instance"""
  r12 = get_ptr(fctx, 0x08).dereference()
  r13 = get_ptr(fctx, 0x10).dereference()
  r14 = get_ptr(fctx, 0x18).dereference()
  r15 = get_ptr(fctx, 0x20).dereference()
  rbx = get_ptr(fctx, 0x28).dereference()
  rbp = get_ptr(fctx, 0x30).dereference()
  rip = get_ptr(fctx, 0x38).dereference()
  rsp = get_ptr(fctx, 0x38)

  return Regs(r12, r13, r14, r15, rbx, rbp, rip, rsp)


class CoroUnwinderSkipFrameFilter:
  instance = None

  @classmethod
  def set_skip_frame_sp(cls, skip_frame_sp):
    if cls.instance is None:
      cls.instance = CoroUnwinderSkipFrameFilter()

    cls.instance.skip_frame_sp = skip_frame_sp

  def __init__(self):
    self.name = "CoroUnwinderSkipFrameFilter"
    self.priority = 100
    self.enabled = True
    gdb.frame_filters[self.name] = self

  def filter(self, frame_iter):
    if not self.skip_frame_sp:
      return frame_iter

    return self.filter_impl(frame_iter)

  def filter_impl(self, frame_iter):
    for frame in frame_iter:
      frame_sp = frame.inferior_frame().read_register("rsp")
      if frame_sp == self.skip_frame_sp:
        continue
      yield frame


class FrameId(object):
  def __init__(self, sp, pc):
    self.sp = sp
    self.pc = pc


class CoroUnwinder(gdb.unwinder.Unwinder):
  instance = None

  @classmethod
  def set_coro(cls, coro):
    set_cur_coro(coro)
    if cls.instance is None:
      cls.instance = CoroUnwinder()
      gdb.unwinder.register_unwinder(None, cls.instance, replace = True)

    cls.instance.coro = coro 

  def __init__(self):
    super(CoroUnwinder, self).__init__("Coro unwinder")
    self.coro = None

  def __call__(self, pending_frame):
    # no unwinding if coro is None
    if not self.coro:
      return None

    # no unwinding if thread is switched
    if CurThread.thread_id() != self.coro.thread.thread_id():
      CoroUnwinder.set_coro(None) 
      CoroUnwinderSkipFrameFilter.set_skip_frame_sp(None)
      return None

    # no unwinding if coro is active 
    if self.coro.is_active:
      self.coro = None
      CoroUnwinderSkipFrameFilter.set_skip_frame_sp(None)
      return None

    orig_sp = pending_frame.read_register('rsp')
    orig_pc = pending_frame.read_register('rip')
    fctx = self.coro.routine['cc_']['ctx_']
    regs = get_regs_from_fctx(fctx)
    rbp = regs.rbp
    rip = regs.rip 
    rsp = regs.rsp 

    frame_id = FrameId(rsp, orig_pc)
    unwind_info = pending_frame.create_unwind_info(frame_id)
    unwind_info.add_saved_register('rbp', rbp)
    unwind_info.add_saved_register('rsp', rsp)
    unwind_info.add_saved_register('rip', rip)

    self.coro = None
    CoroUnwinderSkipFrameFilter.set_skip_frame_sp(orig_sp)

    return unwind_info


class CmdBase:
  """mark command's name so that we can print commmon info"""
  cmd = None

  def __init__(self, cmd):
   self.cmd = cmd

  def invalid_arg_hint_str(self):
    return 'usage or argument error, type \'help {0}\' to \
        find more ...'.format(self.cmd)


class InfoCo(CmdBase, gdb.Command):
  """List coros
  Usage: info co [all|thread_id(begin with 't')|coro_id]
  Example: info co       -> current thread
           info co all   -> all threads
           info co t17456 -> custom thread
           info co 17456 -> custom coro
  """

  def __init__(self):
    CmdBase.__init__(self, "info co")
    gdb.Command.__init__(self, self.cmd, gdb.COMMAND_USER)

  def invoke(self, args, from_tty):
    argv = gdb.string_to_argv(args)
    if len(argv) > 1:
      raise gdb.GdbError(self.invalid_arg_hint_str())

    thread_id = -1
    coro_id = -1
    if len(argv) == 0:
      if not gdb.selected_thread():
        raise gdb.GdbError('No thread selected')
      thread_id = CurThread.thread_id() 
    elif len(argv) == 1:
      if not re.match('(all|t?[1-9][0-9]*)\Z', argv[0]):
        raise gdb.GdbError(self.invalid_arg_hint_str())
        
      if argv[0] == 'all':
        thread_id = -1 
      elif argv[0][0] == 't':
        thread_id =  int(argv[0][1:]) 
      else:
        coro_id = int(argv[0])

    def print_coro(coro):
      titles = ['thread_num', 'thread_id', 'coro_id', 'is_active', 'is_sched', \
          'run_status', 'routine', 'break_function']
      fm = '    '.join('{' + str(_) + ':<' + str(max(len(titles[_]), 10)) + '}' \
          for _ in range(len(titles)))
      if print_coro.print_title:
        print fm.format(*titles)
        print_coro.print_title = False 

      function_name = None
      file_name = None
      line = None
      if coro.is_active:
        function_name = gdb.selected_frame().name()  
        function = gdb.selected_frame().function()
        if function:
          file_name = function.symtab.filename
          line = function.line 
      else:
        fctx = coro.routine['cc_']['ctx_']
        regs = get_regs_from_fctx(fctx)
        rip = regs.rip 
        block = gdb.block_for_pc(int(rip.cast(int_type)))
        function_name = block.function.name
        file_name =  block.function.symtab.filename
        line = block.function.line 
      break_point = ''
      if file_name:
        break_point = '{0} from {1}:{2}'.format(function_name, file_name, line)
      else:
        break_point = function_name
       
      print fm.format( \
          coro.thread.thread_num(), \
          coro.thread.thread_id(), \
          coro.coro_id, \
          'ACTIVE' if coro.is_active else 'NON-ACTIVE', \
          'SCHED' if coro.is_sched else 'NON-SCHED', \
          coro.run_status(), \
          coro.routine, \
          break_point)

    print_coro.print_title = True

    coro_apply(thread_id, coro_id, print_coro)


class Co(CmdBase, gdb.Command):
  """Switch to coro
  Usage: co coro_id 
  Example: co 2
  """
  def __init__(self):
    CmdBase.__init__(self, "co")
    gdb.Command.__init__(self, self.cmd, gdb.COMMAND_USER, prefix = True)

  def invoke(self, args, from_tty):
    argv = gdb.string_to_argv(args)
    id_re_pattern = '[1-9][0-9]*'
    if len(argv) != 1 or not re.match('({0})\Z'.format(id_re_pattern), argv[0]):
      raise gdb.GdbError(self.invalid_arg_hint_str())

    coro_id = int(argv[0])

    def switch(coro):
      gdb.invalidate_cached_frames()
      CoroUnwinder.set_coro(coro)
       
    coro_apply_impl(-1, coro_id, switch)


class CoBack(CmdBase, gdb.Command):
  """Back to initial point for current thread
  Usage: co back 
  """
  def __init__(self):
    CmdBase.__init__(self, "co back")
    gdb.Command.__init__(self, self.cmd, gdb.COMMAND_USER)

  def invoke(self, args, from_tty):
    gdb.invalidate_cached_frames()
    CoroUnwinder.set_coro(None)
    CoroUnwinderSkipFrameFilter.set_skip_frame_sp(None)


class CoApply(CmdBase, gdb.Command):
  """apply command on coros
  Usage: co apply all|thread_id(begin with 't')|coro_id, ... cmd
  Example: co apply all bt 
  """
  def __init__(self):
    CmdBase.__init__(self, "co apply")
    gdb.Command.__init__(self, self.cmd, gdb.COMMAND_USER)

  def invoke(self, args, from_tty):
    argv = gdb.string_to_argv(args)
    id_re_pattern = 't?[1-9][0-9]*'
    if len(argv) < 2 or not re.match('(all|({0}(,{0})*))\Z'.format( \
          id_re_pattern), argv[0]):
      raise gdb.GdbError(self.invalid_arg_hint_str())

    objs = argv[0].split(',')

    def apply(coro):
      do = False 
      for obj in objs:
        if obj == 'all':
          do = True
        elif obj[0] == 't':
          if int(obj[1:]) == coro.thread.thread_id():
            do = True
        elif int(obj) == coro.coro_id:
          do = True
        if do:
          print 'thread_num: {0}, thread_id: {1}, coro_id: {2}'.format( \
              coro.thread.thread_num(), \
              coro.thread.thread_id(), coro.coro_id)
          g_exec(apply.cmd) 
          break

    apply.cmd = ' '.join(argv[1:])
    apply.objs = argv[0].split(',') 

    coro_apply(-1, -1, apply)

InfoCo()
Co()
CoBack()
CoApply()
print '\nHello coro extension for OB!\n'
print 'Doc: 
print 'Commands:'
g_exec('help info co')
g_exec('help co')
