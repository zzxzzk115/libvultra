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

When a project script or gameplay flow needs an engine feature that is not
available in Lua, do not fake the behavior in project content. Record the need
in project `ai/tasks/` or `ai/specs/`, then route an engine task to implement
the missing service/data behavior, add the Lua binding, and update
`doc/lua_scripting.md` plus the project AI notes that rely on it.
