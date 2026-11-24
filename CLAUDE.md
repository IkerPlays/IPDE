# IPDE — notes for Claude

IPDE is IkersStudio's game engine, written from scratch in C++17 (it is NOT based on Godot anymore;
don't copy Godot code). Talk to the owner in Spanish.

## Workflow rules (from the owner)

- Work goes to `master` (push there when an update is finished).
- **After finishing every update, publish a release**: a devbuild by default, or a stable when the
  owner asks for one. Bump `VERSION` first, commit, push to `master`, then run the release workflow and
  watch it until it is published (fix and relaunch if it fails).
- Releases are named `IPDE v.X.Y.Z`, tags `vX.Y.Z-devbuild` / `vX.Y.Z-stable`. Stable releases also get a
  `stable/vX.Y.Z` branch (created by the workflow).
- Target platform: **Windows only** (x86_64). Linux/X11 support exists only to develop and take screenshots.

## 3D engine

`src/render3d` is a CPU rasterizer (Renderer3D) used by the editor viewport and the game; `src/app/scene3d.*`
turns scene entities into draw calls. Scenes have `mode: "3d"` or `"2d"`.

## Versions

- `VERSION` holds `X.Y.Z`. CMake options `IPDE_VERSION` / `IPDE_CHANNEL` override it in CI.
- Bump the patch number for each devbuild, minor for bigger updates.
- Release assets must keep these names (the in-app updater and older versions depend on them):
  `IPDE_vX.Y.Z_windows_x86_64.exe` and `IPDE_vX.Y.Z_windows_x86_64.zip` (containing `IPDE.exe`).

## Publishing a release

```bash
gh api -X POST repos/IkerPlays/IPDE/actions/workflows/release.yml/dispatches -f ref=master -f 'inputs[version]=X.Y.Z' -f 'inputs[channel]=devbuild' -f 'inputs[notes]=...'
```

## Building / testing locally (Linux)

```bash
cmake -S . -B build -G Ninja && ninja -C build
./build/IPDE                     # dashboard
./build/IPDE --open <project>    # editor
./build/IPDE --run <project>     # play a project
```

Screenshots: run under `Xvfb` and capture with `import -window root`.
`IPDE_UPDATER_API=<url>` points the updater to a mock releases endpoint.
