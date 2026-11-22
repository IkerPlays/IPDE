# IPDE logo sources

- `ipde_original.png` — original IPDE artwork.
- `trace.py` — vectorizes the original artwork into `path.txt` (requires `pip install potracer pillow numpy`).
- `gen.py` — generates every branded asset in the repository from `path.txt`
  (editor icons, app icons, splash screens, Windows `.ico`, macOS `.icns`, Android mipmaps…).
  Requires `pip install cairosvg pillow`.

Run both from the repository root:

```bash
python3 misc/logo/source/trace.py && python3 misc/logo/source/gen.py
```
