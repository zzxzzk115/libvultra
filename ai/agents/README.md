# Agent Roles

Suggested engine roles:

- `planner`: turns requests into specs, tasks, and acceptance criteria.
- `implementer`: edits code or assets according to the selected task and skill.
- `verifier`: runs focused checks and records evidence.
- `render-specialist`: handles renderer, shader, frame graph, and render graph work.
- `asset-specialist`: handles asset import, packing, references, and project resources.

Agents should hand off through task files and workspace journals, not through
chat-only memory.
