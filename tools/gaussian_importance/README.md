# Gaussian Importance Training

This folder contains a lightweight importance scorer for the Ordered CLOD
renderer. It follows the useful part of the CLOD-3DGS training recipe: train or
evaluate the model under randomly sampled LOD budgets, then export one scalar
`importance` value per Gaussian. The renderer only needs this order; it does not
need a hierarchy or proxy splats.

## Quick baseline

The default mode is dependency-light and works as a sanity baseline. It scores
splats by opacity, projected footprint, color energy and camera coverage, then
writes an `importance` property into the PLY:

```powershell
python tools/gaussian_importance/train_importance.py `
  --method heuristic `
  --ply resources/training/model/truck/point_cloud/iteration_30000/point_cloud.ply `
  --cameras-json resources/training/model/truck/cameras.json `
  --output resources/training/model/truck/point_cloud/iteration_30000/point_cloud_importance.ply
```

Import `point_cloud_importance.ply` through vasset/libvultra and use Ordered
CLOD mode. Higher `importance` means the Gaussian appears earlier in the prefix.

## LOD evaluation

Use `--eval-only` to render a PLY at several prefix budgets before or after
training. This is the sanity loop for CLOD work: every training change should
show a better PSNR/SSIM curve at low LOD, or at least a better quality/time
tradeoff.

```powershell
python tools/gaussian_importance/train_importance.py `
  --eval-only `
  --ply resources/training/model/train/point_cloud/iteration_30000/point_cloud.ply `
  --scene resources/training/tandt/train `
  --cameras-json resources/training/model/train/cameras.json `
  --downscale 8 `
  --eval-cameras 16 `
  --eval-lods 1.0 0.5 0.25 0.1 0.05 `
  --eval-output build/train_lod_eval.json
```

When the PLY already has an `importance` property, evaluation uses it. Otherwise
it computes the heuristic order first.

## Optional gsplat score training

The `gsplat` mode keeps Gaussian geometry and appearance fixed, then learns one
score per splat by rendering random continuous LOD budgets against the input
images. It is useful as a cheap learned ranking baseline.

```powershell
python tools/gaussian_importance/train_importance.py `
  --method gsplat `
  --ply resources/training/model/truck/point_cloud/iteration_30000/point_cloud.ply `
  --scene resources/training/tandt/truck `
  --cameras-json resources/training/model/truck/cameras.json `
  --output resources/training/model/truck/point_cloud/iteration_30000/point_cloud_importance.ply `
  --iterations 2000 `
  --downscale 4 `
  --train-max-points 600000
```

## CLOD-like prefix finetuning

The `clod` mode is closer to the paper's training recipe. It fixes an ordering
from an existing `importance` property or from the heuristic scorer, randomly
chooses a prefix budget each iteration, renders only that prefix, and finetunes
opacity plus spherical harmonics against both the ground-truth image and a
full-model teacher render. Geometry stays frozen by default, which keeps the
memory cost much lower than full 3DGS retraining.

```powershell
python tools/gaussian_importance/train_importance.py `
  --method clod `
  --ply resources/training/model/train/point_cloud/iteration_30000/point_cloud.ply `
  --scene resources/training/tandt/train `
  --cameras-json resources/training/model/train/cameras.json `
  --output resources/training/model/train/point_cloud/iteration_30000/train_importance_clod.ply `
  --iterations 5000 `
  --downscale 8 `
  --train-max-points 600000 `
  --min-lod 0.05 `
  --max-lod 0.35 `
  --finetune-fields opacity,sh `
  --eval-output build/train_clod_eval.json
```

For a 16 GB GPU, keep `--downscale 8`, start with
`--train-max-points 300000` to `600000`, and make sure `--max-lod` does not ask
for more splats than `--train-max-points` can cover. For example, if the model
has 1.6M splats and `--train-max-points 600000`, keep `--max-lod` around
`0.35` or lower for the first stable run.

The tool uses all SH coefficients when `f_rest_*` properties are present. Use
`--sh-degree 0` only for debugging; the default `--sh-degree -1` uses the PLY's
maximum degree.

Requirements for `gsplat`, `clod`, and `--eval-only` are intentionally not
vendored into libvultra: install a CUDA-enabled PyTorch environment and
`gsplat` in your own conda/venv. On 16 GB GPUs, start with `--downscale 8` to
`16`, keep `--max-lod` below full resolution, and set `--train-max-points 0`
only after smaller runs are stable.

## Windows conda setup

One known-good Windows setup is:

```powershell
conda create -n vultra-clod python=3.10 -y
conda activate vultra-clod
python -m pip install --upgrade pip setuptools wheel
python -m pip install numpy pillow tqdm rich ninja
python -m pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu126
python -m pip install gsplat
conda install -c nvidia cuda-toolkit=12.6 -y
```

For the first `gsplat` run on Windows, launch from "x64 Native Tools Command
Prompt for VS 2022" or initialize MSVC before Python so `cl`, `INCLUDE` and
`LIB` are available:

```bat
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86_amd64
conda activate vultra-clod
set VSLANG=1033
```

The tool sets `CUDA_HOME`, `CUDA_PATH`, `TORCH_EXTENSIONS_DIR`, the conda CUDA
`bin` path and the conda CUDA `lib` path automatically before importing
`gsplat`. The JIT cache is kept under `build/torch_extensions`.

If the first CUDA extension build fails, clear `build/torch_extensions`, verify
that `cl.exe`, `ninja.exe` and `nvcc.exe` are on `PATH`, then rerun from the same
developer prompt.
