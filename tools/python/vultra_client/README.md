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

Visual/offscreen streams use the same Runtime MCP control endpoint plus an HTTP
MJPEG data endpoint:

```python
stream = sim.start_stream(fps=0, jpeg_quality=70, max_height=720)["stream"]
print(stream["url"])

for jpeg in sim.iter_mjpeg_frames(stream["url"], max_frames=8):
    print(len(jpeg))

sim.stop_stream()
```

Start Vultra with:

```bash
xmake run vultra-app \
  --mcp \
  --project example.vproject \
  --render-mode offscreen
```

Use `--render-mode offscreen` for `start_stream()` and visual capture.
Pass `fps=0` to stream every available engine frame, or a positive value to cap
the MJPEG rate. Use `max_width` or `max_height` to cap preview resolution while
preserving aspect ratio; this is usually the fastest browser-preview path.
Omit both caps to stream at native backbuffer resolution. Native resolution is
supported, but it costs more CPU for MJPEG encoding, more browser decode work,
and more bandwidth.

The stream URL can also be started without the Python client:

```bash
curl -s http://127.0.0.1:8848/mcp \
  -H "Content-Type: application/json" \
  --data-binary @- <<'JSON'
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "vultra.render.stream",
    "arguments": {
      "action": "start",
      "fps": 0,
      "jpegQuality": 65,
      "maxHeight": 540
    }
  }
}
JSON
```

Open the returned `stream.url` in a browser. On Windows `cmd.exe`, the same call
can use a readable JSON request file:

```bat
> stream-start.json (
  echo {
  echo   "jsonrpc": "2.0",
  echo   "id": 1,
  echo   "method": "tools/call",
  echo   "params": {
  echo     "name": "vultra.render.stream",
  echo     "arguments": {
  echo       "action": "start",
  echo       "fps": 0,
  echo       "jpegQuality": 65,
  echo       "maxHeight": 540
  echo     }
  echo   }
  echo }
)

curl -s http://127.0.0.1:8848/mcp ^
  -H "Content-Type: application/json" ^
  --data-binary @stream-start.json
```

For local development, create a VS Code Python environment in the repository as
`.venv`. The client has no runtime dependencies, so the venv is only for editor
interpreter selection and smoke scripts:

Linux/macOS:

```bash
export PYTHONPATH=tools/python
python -c "from vultra_client import Simulation; print(Simulation)"
```

Windows PowerShell:

```powershell
$env:PYTHONPATH = "tools/python"
python -c "from vultra_client import Simulation; print(Simulation)"
```
