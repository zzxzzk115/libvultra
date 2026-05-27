# Project AI Workspaces

Every Vultra game project may contain its own tracked AI workspace:

```text
ai/
  README.md
  game.md
  specs/
  tasks/
  workspace/
  knowledge/
  agents/
  generated/
```

Project workspaces hold game-specific intent, content plans, scripts, scene
changes, render graph changes, playtest notes, and generated content records.

Project agents must not directly modify Vultra engine source. If a project task
needs new engine behavior, create an engine task under the engine Harness.
