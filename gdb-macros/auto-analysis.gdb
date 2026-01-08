setup

set logging file bt-all.txt
set logging on
p "EVIL: thread apply all bt"
thread apply all bt
set logging off

set logging file bt.txt
set logging on
p "EVIL: bt"
bt
p "EVIL: info registers"
info registers
p "EVIL: stack size"
stack-size
p "EVIL: bt-full"
bt full
set logging off
