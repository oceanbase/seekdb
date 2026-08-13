#############################################
# Workload aggregator for switchover_roundtrip.test
# This file sources all workload inc files for the switchover roundtrip test.
#
# To add a new workload:
#   1. Create a new inc file (e.g. switchover_roundtrip_xxx.inc)
#   2. Add one --echo + source line below
#   (No changes needed in the framework test file)
#############################################

--echo --- ddl ---
source t/stanby/include/switchover_roundtrip_ddl.inc;
--echo --- fulltext ---
source t/stanby/include/switchover_roundtrip_fulltext.inc;
--echo --- vector ---
source t/stanby/include/switchover_roundtrip_vector.inc;
--echo --- fork_table ---
source t/stanby/include/switchover_roundtrip_fork_table.inc;
