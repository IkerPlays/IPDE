# IPDE — notes for Claude

IPDE is a game engine by IkersStudio, based on Godot Engine (C++, SCons). Talk to the owner in Spanish.

## Workflow rules (from the owner)

- Work goes to `master` (push there when an update is finished).
- **After finishing every update, publish a release**: a devbuild by default, or a stable when the
  owner asks for one. Bump the IPDE version first (see below), commit, push to `master`, then run
  the release workflow and watch it until it is published (fix and relaunch if a job fails).
- Releases are named `IPDE v.X.Y.Z` on GitHub Releases, tags `vX.Y.Z-devbuild` / `vX.Y.Z-stable`.
  Stable releases also get a `stable/vX.Y.Z` branch (created by the workflow).
- Don't show Godot branding in the UI (no Godot donate/support links, versions, names). The MIT
  license notice in About → License must stay.

## Versions

- `version.py`: `ipde_major`, `ipde_minor`, `ipde_patch`, `ipde_channel` are IPDE's own version.
  Keep the file free of comments and blank lines (Gradle parses it line by line).
- The Godot base version fields (`major`, `minor`, `patch`, `status`, `short_name = "godot"`) stay as
  they are for compatibility.
- Bump the patch number for each devbuild (0.1.1 → 0.1.2…), minor for bigger updates.

## Publishing a release

```bash
gh api -X POST repos/IkerPlays/IPDE/actions/workflows/ipde_release.yml/dispatches -f ref=master -f 'inputs[version]=X.Y.Z' -f 'inputs[channel]=devbuild' -f 'inputs[notes]=...'
```

The workflow (`.github/workflows/ipde_release.yml`) builds Linux, Windows, macOS and Android
(release APK signed with the IkersStudio key from secrets) and publishes the assets the in-app
updater (`editor/ipde/ipde_updater.cpp`) expects.

## Building locally

```bash
scons platform=linuxbsd target=editor -j4 wayland=no accesskit=no
```
