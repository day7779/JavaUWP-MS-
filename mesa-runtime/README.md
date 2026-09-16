# Mesa Runtime

Bundled Mesa UWP runtime DLLs for local builds live in this folder.

Required. `Test-MesaRuntimeDir` rejects any folder missing one of these, so `build.ps1` cannot resolve a Mesa runtime without them:

- `opengl32.dll`
- `libgallium_wgl.dll`
- `dxil.dll`
- `spirv_to_dxil.dll`
- `z-1.dll`

Copied when present:

- `vulkan_dzn.dll`

Each DLL the build finds is copied to the package root, to `natives\`, and to `graphics\mesa\`.

Use another Mesa runtime with:

```powershell
.\build.ps1 -MesaRuntimeDir "C:\path\to\mesa-runtime"
```

Or:

```powershell
$env:MESA_UWP_DIR = "C:\path\to\mesa-runtime"
```
