# IPDE

<p align="center">
  <img src="misc/logo/icon.png" width="160" alt="IPDE">
</p>

**IPDE** es un motor de videojuegos 2D y 3D multiplataforma, libre y de código abierto,
basado en [Godot Engine](https://godotengine.org). Mantiene toda la potencia de Godot
(editor visual, GDScript, C#, exportación a escritorio, móvil y web) y añade su propia
identidad, su propio ciclo de versiones y un **actualizador integrado**.

## Versiones

Las versiones de IPDE se publican en
[GitHub Releases](https://github.com/IkerPlays/IPDE/releases) con el nombre
**`IPDE v.X.Y.Z`** y pertenecen a uno de dos canales:

| Canal | Etiqueta Git | Descripción |
|-------|--------------|-------------|
| **Stable** | `vX.Y.Z-stable` | Versión estable. Cada stable tiene además su rama `stable/vX.Y.Z`. |
| **DevBuild** | `vX.Y.Z-devbuild` | Compilación de desarrollo (marcada como *pre-release*). |

### Actualizador integrado

- Al abrir IPDE (Project Manager o editor) se comprueba si hay una **stable** más nueva y
  se pregunta si quieres actualizar (*Actualizar*, *Ahora no* u *Omitir esta versión*).
- Desde **Ayuda → Versiones de IPDE…** (o el botón del Project Manager) puedes instalar
  manualmente cualquier versión: stables, devbuilds, versiones anteriores, etc.
- La descarga, la instalación y el reinicio son automáticos en Windows, macOS y Linux.
  En Android se descarga el APK y se abre el instalador del sistema; en la web basta con
  recargar el editor.
- Se puede configurar en **Configuración del Editor → IPDE → Updates**.

#### Repositorio privado

Las builds oficiales llevan integrado un token de solo lectura para ver las releases del
repositorio privado. Se inyecta al compilar desde el secret `IPDE_UPDATER_TOKEN` (nunca se
guarda en el código):

1. GitHub → Settings → Developer settings → Fine-grained tokens → *Generate new token*.
2. *Repository access*: **Only select repositories** → `IkerPlays/IPDE`.
3. *Permissions → Repository permissions → Contents*: **Read-only**.
4. Guárdalo en **Settings → Secrets and variables → Actions** como `IPDE_UPDATER_TOKEN`.

El token va ofuscado dentro del ejecutable, pero alguien con conocimientos podría extraerlo
y leer el código del repositorio. Si caduca o lo revocas, cualquiera puede poner uno nuevo en
**Configuración del Editor → IPDE → Updates → Github Token**, que tiene prioridad.

## Compilar desde el código

IPDE se compila igual que Godot (SCons):

```bash
scons platform=linuxbsd target=editor
```

Consulta la [documentación de compilación de Godot](https://docs.godotengine.org/en/latest/engine_details/development/compiling/index.html)
para cada plataforma. La versión de IPDE está en [`version.py`](version.py)
(`ipde_major`, `ipde_minor`, `ipde_patch`, `ipde_channel`).

## Publicar una versión

En GitHub: **Actions → IPDE Release → Run workflow**, indica la versión (`X.Y.Z`) y el
canal (`stable` o `devbuild`). El flujo compila el editor para Linux, Windows, macOS
y Android, crea la release `IPDE v.X.Y.Z` con los binarios y, si es stable, crea la
rama `stable/vX.Y.Z`.

### Firma de Android

Para que cada APK nuevo se instale encima del anterior, todos deben firmarse con la misma
clave. Configura estos secrets en **Settings → Secrets and variables → Actions**:
`IPDE_ANDROID_KEYSTORE_BASE64`, `IPDE_ANDROID_KEYSTORE_ALIAS` e
`IPDE_ANDROID_KEYSTORE_PASSWORD`. Sin ellos se usa una clave temporal.

## Licencia y créditos

IPDE se distribuye bajo la licencia MIT, igual que Godot Engine.
Godot Engine © 2014-present Godot Engine contributors, © 2007-2014 Juan Linietsky,
Ariel Manzur. Consulta [LICENSE.txt](LICENSE.txt), [COPYRIGHT.txt](COPYRIGHT.txt) y
[AUTHORS.md](AUTHORS.md).
