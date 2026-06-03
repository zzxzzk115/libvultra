# C++ Conventions

Stable engineering conventions for the C++ core. Keep edits consistent with these.

## assert vs error handling

`assert` compiles out in Release, so it must only guard **unreachable invariants** —
conditions that are impossible unless there is a programming bug. It must **not** be the
handling path for **external or recoverable** errors (missing files, failed device
features, bad user/script input, network/IO failure), because those would then fail
silently in Release.

- Invariant (keep `assert`): e.g. `assert(m_Impl)` in `core/rhi/buffer.cpp` — calling a
  method on a moved-from/empty `Buffer` is a caller bug; a separate `operator bool()`
  exists for the legitimate "is this valid?" check.
- External/recoverable (do **not** use `assert`): return a `vbase::Result`/status,
  return an error value, or throw — and log. Give the caller something to act on.

When auditing a site, ask: "can this fire from correct code given valid external
inputs?" If yes, it is recoverable and must not be an `assert`.

## Ownership and RAII

The codebase is RAII-first (`unique_ptr`/`shared_ptr`, `Ref<>`). Avoid raw `new`/`delete`
for owned resources; they leak on exception paths and contradict the surrounding style.

- Prefer `std::make_unique` / `std::make_shared`. For a `unique_ptr` member of a
  forward-declared type, declare the owner's destructor in the header and `= default` it
  in the `.cpp` (where the type is complete) so the deleter instantiates correctly.
- **Exception:** third-party ownership contracts that require raw allocation. Example:
  JoltPhysics `JobSystem::CreateJob` must `new` a ref-counted `Job` and hand it to a
  `JobHandle`; Jolt calls `FreeJob` back to `delete` it. Do not "RAII-ify" these — the
  library owns the lifetime. Mark such sites with a short comment explaining why.

Do not enable a blanket `cppcoreguidelines-owning-memory` clang-tidy check while
`WarningsAsErrors:*` is set: it would fail the legitimate third-party-contract sites
above (and likely other unaudited spots) and break the build. Convert real offenders
case by case instead.

## God files

Keep new translation units focused. When a file grows past a few thousand lines and
mixes responsibilities, split it by responsibility rather than adding more. See
`doc/architecture/render-system.md` and `doc/architecture/asset-system.md` for the
in-flight split plans.
