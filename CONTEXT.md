# seekdb configuration

This context describes the settings that control a running seekdb instance.

## Language

**Instance parameter**:
A named setting that controls the seekdb instance and has a declared default and validation rule. It is distinct from a SQL session system variable.
_Avoid_: Session variable, system variable

**Persistent override**:
A saved value for a declared instance parameter that takes precedence over its declared default when the instance starts.
_Avoid_: Default value

**Effective instance value**:
The value currently used by the running instance. For a parameter that requires restart, it can differ from the persistent override until the next start.
_Avoid_: Saved value
