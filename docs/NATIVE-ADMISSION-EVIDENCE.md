# Native service admission receipts

Status: Instrumentation implemented; fresh pinned ext4 run and explicit scoped
acceptance remain pending. This does not extend the provider acceptance for
`6a6337d`.

## Scope and format

The existing binding and native service test targets emit receipts after their
operations. `NativeStoreBinding.hpp` records selected observations in a test-only
256-entry fixed buffer. There is no allocation, logging or caller callback in
that collector. Reset collection between named cases. An overflow makes trace
verification fail. A separate synthetic overflow test proves that the buffer
sets its overflow flag rather than silently accepting an incomplete trace.

All receipt fields and accessors compile only with `OPENHDK_ENABLE_TEST_SEAMS`.
The production factory, fence ordering and save protocol are unchanged. The
fixed buffer is separate test instrumentation, not an additional production
payload allowance or a total process-memory bound. Collection is serialized;
it is not a concurrent tracing interface.

| Record | Meaning |
| --- | --- |
| `ADMISSION_TRACE` | Case tag, exact selected-record count and overflow flag. |
| `ADMISSION_RECORD identity-mount` | Successful fstat/statx observation: device, inode, mount ID and optional root index. |
| `ADMISSION_RECORD epoch-check` | Owner equality, held lease and epoch equality before authority rejection. No pointers or epoch tokens are exposed. |
| `ADMISSION_RECORD binding-export` | Fresh export (`index=0`) or cached export (`index=1`). |
| `ADMISSION_RECORD ancestor-step` | Retained-directory ancestry identity and zero-based step. |
| `ADMISSION_RECORD ancestor-stop` | Mount boundary (`index=0`) or self-parent (`index=1`). |
| `ADMISSION_RECORD failure` | Structured error code, optional root index and nested native errno when available. |
| `BINDING_CASE` | Checked mapping/cache/CLOEXEC, overlap, mount or epoch outcome. |
| `FENCE_CASE` | Initial/final admission-check count, outcome and primary/memory preservation. |
| `SERVICE_PRIMARY` | Full-file SHA-256, byte count and decoded sequence/revision of the tiny harness checkpoint. |
| `SERVICE_ALLOCATION` | Factory failure count, eventual success and before/after FD counts. |

`operation` and `error_code` use `NativeBindingOperation` and
`NativeBindingErrorCode` from `NativeStoreBinding.hpp`. They are distinct from
store outcome enums. `index=none` denotes no index; it must not be treated as
root zero. `errno=0` on a semantic rejection does not imply a successful syscall.
Failure records preserve errno only when the error carries a provider error.
These are selected semantic observations, not a complete syscall trace.

Missing mount IDs and differing root mount IDs are injected and marked
`injected=1`. The actual `/proc` mount rejection is separate. The forced lexical
bypass used to exercise handle ancestry is also marked on ancestor-step records.
Root replacement uses actual rename/create operations in a harness-owned tree.
It precedes the final identity check; it does not prove atomic pathname CAS or
hostile-writer isolation. The private mount-namespace alias case remains
`Skipped` because that harness has not been implemented.

## Cases and verification

The binding target covers fresh/cached export, sibling admission and ancestor
termination, equal/parent overlap, foreign owner, mount rejection, replaced
root and release/reacquisition. Retained old capabilities cannot authorize a
new acquisition. The service target covers baseline creation, override save,
NoChange, restored unattached roots, overlap with RootId mapping, late changed
and NoChange fence drift, injected uncertain sync and allocation/FD cleanup.

The late changed operation observes two admission checks: one before staging
and one at the coordinator's final fence. The NoChange case also observes two.
The tests compare exact primary bytes and immutable snapshot identity before
printing preservation receipts. Primary hashes before/after give a durable
consistency record without printing username-bearing checkpoint hints or raw
path bytes. No authority token is serialized. Hashes do not prove provenance,
freshness, authentication or power-loss durability.

`tests/verify-native-admission-receipts.py` checks framing/counts, overflow,
identity fields, fresh/cache exports, epoch rejection, actual/injected mount
failures, ancestor termination, fence cases, checkpoint hash/size/sequence/revision
preservation and allocation/FD cleanup. It is a consistency verifier, not an
independent execution authenticator. The collector records its exit code and
includes its output in the existing SHA256SUMS log manifest. Python 3 is required.

## Fresh native procedure

After this instrumentation is reviewed and merged, use a clean checkout pinned
to the full tested commit SHA. Use an existing WSL2 ext4 directory outside the
checkout. Run:

```bash
bash tests/run-linux-checkpoint-evidence.sh FULL_COMMIT_SHA EXISTING_EXT4_PARENT
```

`OPENHDK_NATIVE_CHECKPOINT_DIR` is set by the collector. Binding and native
service tests then use production filesystem eligibility rather than the private
overlay bypass. Local overlay runs provide regression evidence only. Preserve
compile/run/verify/overall exits, environment and source logs, and both admission
target logs. Apply the documented username-redaction hash/replacement procedure
to new bundles; do not rewrite historical evidence.

Fresh evidence must be reviewed for its exact revision and environment. Keep
native factory acceptance pending until an explicit scoped decision. Windows
checkpoint acknowledgment remains blocked; durable root lifecycle, application
wiring, NCN evidence and manual validation remain separate work.

See [native binding plan](NATIVE-SERVICE-BINDING-PLAN.md) and
[provider evidence](NATIVE-LINUX-CHECKPOINT-EVIDENCE.md).
