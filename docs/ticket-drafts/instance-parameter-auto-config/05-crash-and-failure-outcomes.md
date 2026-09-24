# 05: Make crash and failure outcomes observable

**What to build:** Operators receive accurate outcomes when saving or reloading fails, and an interrupted save never leaves a partial configuration file. The guarantee is verified through the deployed instance boundary, including concurrent saves and process termination around replacement.

**Blocked by:** 02: Switch declared instance parameters to the Rust auto-config store.

**Status:** Approved draft; tracker publication pending.

- [ ] Concurrent in-process saves preserve every completed change.
- [ ] Failures or process termination around temporary-file sync, replacement, new-target sync, and directory sync leave a complete old or new file that can be read on restart.
- [ ] A failure before replacement leaves the old file authoritative. After replacement, a sync failure is reported without claiming the old file was restored.
- [ ] If persistence succeeds but immediate reload fails, the saved override remains and the response explicitly says it was saved but could not be applied.
- [ ] If a multi-parameter command is interrupted between parameters, its completed prefix may remain saved, and every resulting file is complete.
