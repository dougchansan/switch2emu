# Foreground-rights acquisition in the reference profile

This milestone implements `IWindowController::AcquireForegroundRights` (command
10) for the explicit `--hle-reference --homebrew-entry` profile. It advances the
unchanged bundled libnx application past its previous startup blocker. It does
not establish native Switch 2 compatibility, implement a GPU/display layer, or
complete application initialization.

## Reference and policy

The pinned public libnx revision is
`b156b4774fb2df7b93402169509266da7547fc4c`.
[Its application startup](https://github.com/switchbrew/libnx/blob/b156b4774fb2df7b93402169509266da7547fc4c/nx/source/services/applet.c#L263-L307)
waits for `AppletFocusState_InFocus`, calls `AcquireForegroundRights`, then calls
`SetFocusHandlingMode`.
[The command wrapper](https://github.com/switchbrew/libnx/blob/b156b4774fb2df7b93402169509266da7547fc4c/nx/source/services/applet.c#L1269-L1278)
specifies WindowController command 10 with no input or output payload.

These sources establish the client request and ordering. They do not establish
the console's complete rights, revocation, or error policy. The implementation
therefore uses the following explicit **local reference policy**:

| State or request | Result |
| --- | --- |
| Live, focused process without acquired rights | Acquire rights after writing a successful empty reply |
| Live, unfocused process without acquired rights | Return the runtime's `InvalidState`; leave focus and rights unchanged |
| Process already owns rights | Return success idempotently, including after a later focus loss |
| Extra payload or the supported PID-plus-copied-handle request form | Return `InvalidArguments`; leave rights unchanged |
| Invalid object or different interface | Preserve existing invalid-object or unknown-command behavior |
| Unwritable IPC reply or malformed request | Return the existing transport error; publish no state change |
| Exited or faulted process | Clear acquired rights and reject later acquisition |

Rights belong to the process, independently of the session or domain object
used to request them. Cloned sessions, independently opened applet connections,
and reopened WindowController objects see the same acquisition. Closing those
sessions does not revoke it.

Acquired rights and current focus are separate values. Acquisition does not
change host focus or create a focus message. Host focus changes still use the
existing bounded notification queue and do not revoke rights. The local error
for first acquisition while unfocused is a documented policy choice, not a
verified Horizon result code for that condition.

Successful reload starts a fresh process without rights. A failed load keeps
the existing process, its current focus, and its acquired rights intact.

## Observed startup boundary

The bundled NRO is unchanged:

```text
tests/programs/libnx_app/program.nro
SHA-256: 10F4CF1CB2CD69FCB75C8935CB39A9F84210114236688BB50F3495AF2409553F
```

The original libnx client now receives success from WindowController command
10. Its next unsupported request is **`ISelfController::SetFocusHandlingMode`
(command 13)**. The client sends three boolean bytes `(0, 0, 1)` during startup
and `(1, 1, 0)` during failure cleanup. Both calls remain explicit
`UnknownCommand` results. The final diagnostic is still
`LibnxError_InitFail_AM` (16217), so the diagnostic code alone cannot distinguish
the old blocker from the new one.

Constructors and `main` are still not reached. The fixture retains its original
libnx startup and explicit traps for later missing HID, time and filesystem
dependencies. No fixture binary, SDK source or firmware version was changed to
obtain this advance. See the updated
[reference manifest](../tests/programs/libnx_app/reference.json) for observed
instruction and IPC counts.

The focused Debug validation of this continuation measured:

| Execution | Prior milestone | Foreground-rights milestone |
| --- | --- | --- |
| Starts focused | 7,428 instructions; 35 IPC exchanges | 7,732 instructions; 36 IPC exchanges |
| Starts unfocused, before waiting | 7,178 instructions | 7,178 instructions |
| Resumes after host focus gain | 1,553 instructions | 1,857 instructions |
| Final diagnostic | 16217 (`InitFail_AM`) | 16217 (`InitFail_AM`) |
| First unsupported command | WindowController 10 | SelfController 13 |

These counts describe the supplied fixture and its diagnostic path, not game
compatibility or a performance benchmark. The three affected suites and the
virtual CPU-counter suite passed in that focused run.

The next milestone should implement and validate the required focus-handling
behavior before extending the rest of applet startup. `NotifyRunning`, operation
and performance mode queries, and notification settings follow in the pinned
client. A blanket successful reply would conceal those missing behaviors.

## Verification

`applet_identity_messages_and_native_wait` covers first acquisition while
focused and unfocused, malformed/extra/PID requests, wrong objects, protected
response memory, idempotence, shared session lifetime, focus-message behavior,
terminal states, and successful/failed reloads. It also preserves the original
libnx out-of-focus wait and host-driven wakeup.

`libnx_default_app_and_native_apm` captures the actual IPC trace and requires a
successful command 10 followed by exactly the two command 13 failures described
above. It still checks that no constructors or `main` ran, that cleanup closes
the original handles, and that small instruction budgets reproduce the result.

`native_applet_domains_and_lifetimes` checks the additional successful empty
domain reply and preserves the existing object ownership and cleanup checks.
