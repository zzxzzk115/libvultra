# Vultra AI Harness

This directory is the tracked AI collaboration layer for Vultra engine development.
It is separate from `.vultra/`, which remains local runtime/editor state.

## Layers

- `specs/` stores durable engine specs and design decisions.
- `tasks/` stores task-centered execution plans with scope and acceptance criteria.
- `workspace/` stores journals, handoff notes, verification logs, and proposed patches.
- `knowledge/` stores stable project facts that agents should reuse across sessions.
- `agents/` defines agent roles and handoff rules.
- `skills/` indexes repository skills without duplicating their source files.

## Workflow

1. Pick or write a spec before implementation starts.
2. Split work into one focused task under `tasks/`.
3. Select a relevant skill from `skills/index.md`.
4. Implement in a clean context and keep unrelated user changes intact.
5. Verify with the smallest command set that proves the change.
6. Record results and next handoff notes under `workspace/`.

Project-level game creation uses the same shape inside each Vultra game project,
but project knowledge must stay in the project workspace.
