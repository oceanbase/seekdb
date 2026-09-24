# 01: Consolidate instance parameter persistence access

**What to build:** Keep the current operator-visible instance parameter behavior while routing reads and writes through the configuration manager. Internal consumers, including saved server role information, should no longer depend on a particular storage object. This prepares one clear boundary for replacing the store.

**Blocked by:** None (can start immediately).

**Status:** Approved draft; tracker publication pending.

- [ ] Existing instance parameter settings still survive restart and appear as the effective instance value where applicable.
- [ ] Internal server role information still saves and loads across restart through the configuration manager.
- [ ] No internal caller needs direct access to the current SQLite parameter storage object.
- [ ] Existing SQL setting and reload behavior remains unchanged, as shown by instance-level tests.
