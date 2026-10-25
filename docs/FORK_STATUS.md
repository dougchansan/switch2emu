# Fork status and continuation

Reviewed on 25 October 2026. This fork continues
[PHENGOX/Switch2Emu](https://github.com/PHENGOX/Switch2Emu) from upstream commit
`3c74a1412400bbe5a10728122685770cde0b89e0`. The original implementation and
attribution are retained. Project code remains GPL-3.0-only; bundled reference
programs retain the notices in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

**Current maturity: an ARM64 reference interpreter and bounded homebrew/HLE
research prototype. No commercial game or native Switch 2 application has been
validated.** Passing a Switch/libnx test does not establish native Switch 2
compatibility.

This page is the current summary. Earlier documents describe successive
milestones; their instruction counts, suite counts and next-task statements
record the state at that milestone.

## What is implemented

| Area | Working implementation | Remaining boundary |
| --- | --- | --- |
| CPU | W/X integer operations, selected atomics, scalar/vector FP and SIMD, software FP handling, saved contexts | Partial AArch64 instruction coverage; interpreter only, no JIT; sequential execution model |
| Memory | Sparse 4 KiB backing pages, R/W/X checks, protected stack and RELRO, checked bus access | Synthetic 16 MiB address space; no console virtual-memory model or MMU |
| Loaders | Static AArch64 ELF64 and a bounded NRO0/MOD0 homebrew profile | Extended NRO variants and asset trailers are rejected; no NSO/NCA/NSP/XCI game-loading path |
| Runtime | One main thread plus one worker, typed handles, events, waits, virtual time, basic memory services | Bounded reference ABI; not a complete Horizon kernel or multicore scheduler |
| IPC/services | HIPC/CMIF subset, buffers, session cloning, domains, service manager, APM session creation and selected applet operations | Most native services and method families remain unsupported |
| libnx startup | Original crt0, dynamic and initialization components run in reproducible harnesses; default app initialization reaches real applet calls | The default app fixture still stops before constructors and main; it is a partial SDK build |
| Graphics/input | CPU-generated RGB frames via project-specific emu:disp, PNG export, Windows GDI window and local emu:pad keyboard input | No native guest GPU command decoding, shader translation, Vulkan renderer or native HID implementation |
| Audio | Applet audio interface objects can be created | No audio methods, mixing or playback backend |

The source of these findings is the implementation in [src](../src), exercised
by [tests](../tests), rather than the long-term GPU and compatibility plans.

## First continuation

1. Fix the atomic test's mixed integer initializer-list types, which prevent a
   GCC build on Linux's LP64 data model. The test values remain identical.
2. Add Linux and Windows Debug/Release CI with Git LFS enabled so all checked-in
   CPU reference data are available. The workflow uses read-only repository
   permissions and a pinned checkout action.
3. Implement stateful `IWindowController::AcquireForegroundRights` in the
   explicit homebrew reference profile. Request validation, response-write
   atomicity, process lifetime, repeated calls and focus changes are tested.
   See [foreground-rights semantics and evidence](FOREGROUND_RIGHTS.md).
4. Keep the unchanged libnx NRO as the integration target and require its trace
   to identify the next missing call: `ISelfController::SetFocusHandlingMode`
   (command 13). The high-level AM initialization error remains 16217; that
   error alone cannot distinguish the old and new stopping points.

The foreground policy is a documented local reference policy. It does not
claim that Horizon or native Switch 2 uses the same failure or revocation
behavior. Unsupported operations continue to return explicit errors.

## Build and reproduce

Install Git LFS before cloning, or run `git lfs pull` in an existing checkout.
The three LFS CPU corpora total approximately 443 MiB. Without them, the full
reference tests cannot pass. The guest fixtures are already included; their
Zig/Python/Unicorn regeneration toolchain is not required to build the emulator.

```sh
git lfs pull
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel 2
ctest --test-dir build/debug --output-on-failure --no-tests=error

cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 2
ctest --test-dir build/release --output-on-failure --no-tests=error
```

With Visual Studio, add `--config Debug` to the build command and `-C Debug`
to CTest (or use `Release` for both). The interactive native window requires
Windows; the command-line core and non-window tests can run on Linux.

The upstream default-app baseline was reproduced on Linux: 7,428 guest
instructions, 35 IPC exchanges, AM error 16217, no main. Its NRO SHA-256 is
`10f4cf1cb2cd69fcb75c8935cb39a9f84210114236688bb50f3495af2409553f`.
The continuation keeps these guest bytes unchanged. Current trace counters
belong in [the fixture manifest](../tests/programs/libnx_app/reference.json).

## Validation of this continuation

Local Linux validation used GCC 13.3.0 and CMake 3.31.6:

| Configuration | Result |
| --- | --- |
| Debug, complete CTest suite | 60/60 passed |
| Release, complete CTest suite | 60/60 passed |
| Focused applet, domain, counter and default-app tests | 4/4 passed |
| Default-app trace | 7,732 instructions, 36 IPC exchanges, command 10 succeeds, command 13 remains unsupported |
| Guest fixture integrity | NRO SHA-256 unchanged; all three CPU LFS objects match their recorded object hashes |

The CI workflow also defines Windows Debug and Release jobs. Their hosted
results must be checked separately; the local Linux runs do not validate the
Windows interactive window. The reference cases validate selected behaviors,
not complete AArch64 compliance or game compatibility.

## Next milestones and acceptance criteria

| Order | Deliverable | Evidence required |
| --- | --- | --- |
| 1 | Continue default app initialization | Implement the next observed applet method with state and negative tests; record each new stopping point |
| 2 | Reach main through the intended startup path | No substituted success responses or missing-dependency traps; deterministic constructor/main markers and cleanup |
| 3 | Run a normal public libnx hello-world | Pin its source and toolchain; support the actual display/input dependencies without rewriting the program to emu:* |
| 4 | Support a useful application address space | Separate virtual mappings, physical backing and reservation tracking; test sparse high addresses and resource limits |
| 5 | Establish a native Switch 2 reference target | Record provenance, version, entry ABI and observable reference behavior; distinguish it from Switch 1 compatibility mode |
| 6 | Add guest graphics and assess CPU performance | Validate guest command/shader results and synchronization; profile a meaningful workload before selecting a fast backend |

Several HID, time, filesystem and runtime dependencies beyond the current
startup boundary are explicit traps in
[libnx_app/missing.S](../tests/programs/libnx_app/missing.S). Advancing one
applet method does not remove those dependencies.

Two scalability findings matter before expanding toward games. Reservation
tracking currently allocates an eight-byte version for every sixteen bytes of
addressable RAM, even when the backing pages are sparse. Also, each ordinary
instruction snapshots the CPU context for failure rollback. These choices are
useful in the present reference machine but require measurement or redesign
before large address spaces and high throughput are realistic goals.

There is no reliable completion percentage or playable-game schedule at this
stage. Each milestone should be judged by an executable reference and an
observable result.
