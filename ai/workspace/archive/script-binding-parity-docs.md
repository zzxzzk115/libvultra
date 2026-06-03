# Script Binding Parity Docs

## Summary

Added the repository and project-agent rule that gameplay-facing engine work
must check script binding parity:

- decide whether players need Lua access;
- audit nearby same-subsystem gaps;
- implement missing service/data behavior before Lua bindings;
- keep bindings thin over services/components;
- update player Lua docs plus repository and project-local AI docs.

Project templates now write the project AI workspace through one shared helper,
and both `empty` and `minimal` projects receive the AI workspace files.

## Verification

- `git diff --check -- AGENTS.md ai/README.md ai/specs/ai-harness.md ai/knowledge/lua-scripting.md ai/knowledge/project-workspaces.md ai/skills/binding-gen/SKILL.md doc/lua_scripting.md source/vultra_app/src/project_templates.cpp`
- `xmake build -y vultra-app`
