#############################################
# Workload aggregator for standby_restart.test
# This file sources all workload inc files for the standby restart test.
#
# To add a new workload:
#   1. Create a new inc file (e.g. standby_restart_xxx.inc)
#   2. Add one --echo + source line below
#   (No changes needed in the framework test file)
#############################################

--echo --- ddl ---
source t/stanby/include/standby_restart_ddl.inc;
--echo --- fulltext ---
source t/stanby/include/standby_restart_fulltext.inc;
--echo --- vector ---
source t/stanby/include/standby_restart_vector.inc;
--echo --- fork_table ---
source t/stanby/include/standby_restart_fork_table.inc;
