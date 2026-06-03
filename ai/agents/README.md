# Agent Roles

Suggested engine roles, with selection criteria and the skills they typically drive.

| Role | Select when the work is… | Primary skills |
|---|---|---|
| `planner` | turning a request into specs/tasks/acceptance criteria, or prioritizing | — |
| `implementer` | editing code/assets for a selected task | `binding-gen`, `vultra-component-workflow` |
| `verifier` | running focused checks and recording evidence | — |
| `render-specialist` | renderer, shader, frame graph, or render graph work | `vultra-lua-render-pass` |
| `asset-specialist` | asset import, packing, references, project resources | — |

## Selection

1. Classify the request by subsystem (gameplay/Lua, ECS components, rendering, assets).
2. Pick the role whose "select when" matches; pick the skill whose `in_scope`
   (see each `ai/skills/*/agents/openai.yaml`) matches and whose `out_of_scope`
   does not. If two skills overlap, prefer the more specific one.
3. If no skill fits, proceed as `implementer` from first principles and consider
   promoting the repeated workflow into a new skill afterward.

## Interaction model

- One role owns a task at a time. Hand off through `ai/tasks/` files and
  `ai/workspace/` journals (use `ai/workspace/TEMPLATE.md`), not chat-only memory.
- `planner` produces/updates the task; `implementer`/specialists execute a phase;
  `verifier` records evidence; the journal's Status/Next steps drive the next role.
- Respect the engine/project harness boundary in `AGENTS.md`.
