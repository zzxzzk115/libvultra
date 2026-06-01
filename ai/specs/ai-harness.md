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

Project agents may rely on gameplay scripting APIs only after the repository
Lua documentation and project-local AI notes describe those APIs. If a game
workflow needs an engine feature that has no Lua binding, the engine task must
first add the missing service/data behavior and then expose a thin Lua binding.

## Automation Layer

Repository and project Harness context lives in tracked `ai/` folders and is
read directly by agents before planning or editing. Live editor/runtime
automation is handled by the C++ Runtime MCP embedded in `vultra-app`; it is for
running-engine diagnostics and editor commands, not for bypassing Harness safety
rules around source or project-content edits.

## Acceptance Criteria

- New projects receive a project-level AI workspace skeleton.
- Agents can inspect engine and project workspaces from the tracked `ai/`
  files.
- Runtime MCP can drive live editor automation without directly mutating engine
  source.
- Project-content writes still follow explicit approval, dry-run, and handoff
  notes when requested by the project workflow.
- Gameplay-facing engine changes include a scripting parity decision, Lua docs,
  and project AI doc updates when relevant.
