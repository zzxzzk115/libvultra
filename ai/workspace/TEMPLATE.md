# <Short Title>

Date: <YYYY-MM-DD>
Status: <In-Progress | Blocked | Done>

## Summary

One short paragraph: what this note covers and why it exists. Link to the durable
spec/task instead of copying them, e.g. `ai/specs/<x>.md`, `ai/tasks/<x>.md`.

## Result

What was actually done / decided / verified. For verification logs, list the exact
commands run and their outcome.

## Next steps

Concrete follow-ups. Omit or write "none" when Status is Done.

## Blockers

What is blocking progress, if anything. Omit or write "none" when unblocked.

---

Conventions:

- Every workspace note starts with `Date:` and `Status:` so the index and any future
  auto-archiver can sort and reap by status/age without reading the whole file.
- When a note reaches `Status: Done` and its work has landed, move it to
  `ai/workspace/archive/` and update `ai/workspace/README.md`.
- Keep notes short. The durable home for ongoing plans is `ai/tasks/`; for stable
  facts, `ai/knowledge/`.
