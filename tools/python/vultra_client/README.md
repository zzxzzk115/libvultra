# vultra_client

Tiny standard-library Python client for the Vultra Runtime MCP/RPC endpoint.

```python
from vultra_client import Simulation

sim = Simulation.connect("http://127.0.0.1:8848/mcp")
print(sim.status())
sim.reset(seed=42)
obs = sim.step(frames=4)
print(obs["state"]["entities"])
```

Start Vultra with:

```powershell
xmake run vultra-app -- --editor --rpc --project example.vproject --render-mode none
```

For local development, create a VS Code Python environment in the repository as
`.venv`. The client has no runtime dependencies, so the venv is only for editor
interpreter selection and smoke scripts:

```powershell
$env:PYTHONPATH = "tools/python"
python -c "from vultra_client import Simulation; print(Simulation)"
```
