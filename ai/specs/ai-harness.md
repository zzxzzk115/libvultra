# Vultra Dual-Layer AI Harness

## Intent

Vultra should support two separate AI collaboration layers:

- Engine Harness for developing Vultra itself.
- Project Harness for creating games inside Vultra projects.

The engine layer defines capabilities and rules. The project layer stores game
intent and content state. These layers may hand work to each other through tasks,
but their knowledge must not be mixed.

## Engine Layer

The engine Harness lives in the repository-level `ai/` directory and manages
specs, tasks, knowledge, agent roles, skill selection, verification, and handoff.

## Project Layer

Each Vultra project may include a project-level `ai/` directory with `game.md`,
specs, tasks, workspace notes, project knowledge, agent roles, and generated
content records.

Project agents may edit project content through controlled tools. They must not
directly edit engine C++ source.

## MCP Layer

`tools/vultra_mcp/` exposes engine and project context through MCP resources,
prompts, and tools. Edits default to dry-run and require explicit write mode,
hash checks, and path allowlists.

## Acceptance Criteria

- New projects receive a project-level AI workspace skeleton.
- MCP can inspect engine and project workspaces.
- MCP dry-run tools produce proposed patches without mutating files.
- MCP write mode rejects missing hashes, stale hashes, and paths outside the
  allowlist.
