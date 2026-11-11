# Windows bootstrap timeout troubleshooting

## Symptom

On Windows, a fresh seekdb data directory can occasionally fail during the
first `seekdb.Open` call. The native process exits with code 1 before the
server becomes ready, and the server log contains:

```text
ret=-4012
OB_TIMEOUT
```

The failure happens before application SQL is executed. It has been observed
intermittently on Windows 11 with seekdb 1.4.0.

## Safe retry sequence

1. Stop the seekdb process and confirm no native process is still running.
2. Remove only the newly created data directory for the failed attempt.
3. Clear the native extraction cache if the attempt used a fresh cache.
4. Retry with a new data directory and capture the complete server log.

Do not delete an existing database directory that contains data you need.

## What to collect

- Windows version and CPU architecture.
- seekdb package version and native archive SHA-256.
- The complete server log around `ret=-4012`.
- Whether the data directory and native extraction cache were newly created.
- The number of consecutive attempts and their outcomes.

## Notes

This page documents a workaround and diagnostic checklist. The underlying
intermittent bootstrap failure still needs a code-level root cause.
