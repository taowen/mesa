# taowen/mesa (Ardesk)

Fork of [lfdevs/mesa-for-android-container](https://github.com/lfdevs/mesa-for-android-container)
(Mesa + Adreno KGSL). Ardesk uses this tree so a **glibc** process can talk to
Qualcomm KGSL without going through Android `libGLES` / `libvulkan`.

| API | Driver | Kernel |
| --- | --- | --- |
| OpenGL / GLES | Freedreno (Gallium) | `src/freedreno/drm/kgsl` → `/dev/kgsl-3d0` |
| Vulkan | Turnip | `src/freedreno/vulkan/tu_knl_kgsl.cc` → same node |

`-Dfreedreno-kmds=kgsl` builds both. Gallium loads as `kgsl_dri.so`
(`MESA_LOADER_DRIVER_OVERRIDE=kgsl`). This is the path Winlator skipped in
favor of Zink-on-Turnip: Turnip’s KGSL winsys was upstream; Freedreno’s
(Lucas Fryzek / Igalia, [MR 21570](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21570))
was not, because Android UI gralloc surfaces faulted the IOMMU. Ardesk
presents guest windows through anlabwc + AHB, not SurfaceFlinger, so that
blocker does not apply.

This does nothing on Mali. X300 stays on Gladio / Vortek.

Build sketch:

```
meson setup build --cross-file android-or-glibc.ini \
  -Dgallium-drivers=freedreno \
  -Dvulkan-drivers=freedreno \
  -Dfreedreno-kmds=kgsl \
  -Dplatforms=x11,wayland \
  -Dglx=dri -Degl=enabled
```
