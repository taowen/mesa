# Mesa for Android Container
Forked From [Mesa - The 3D Graphics Library](https://gitlab.freedesktop.org/mesa/mesa)

**English** | [Simplified Chinese](docs/zh-HanS/README.md) | [Traditional Chinese](docs/zh-HanT/README.md)

---

A [Mesa](https://gitlab.freedesktop.org/mesa/mesa) build for containers on Android (PRoot, Chroot, LXC, Droidspaces, etc.), to support hardware acceleration with Adreno GPU.

## Features
  - The Freedreno and Turnip driver in Mesa 26.0.0 and later versions supports Adreno 8xx GPU.
  - Mesa drivers compiled in ARM64 Docker containers across multiple popular Linux distributions offer better compatibility and can be used in PRoot, Chroot, Droidspaces, and LXC containers hosted on Android.
  - For some Adreno 6xx/7xx/8xx GPUs, the Freedreno driver can be used for OpenGL, OpenGL ES, and Vulkan, eliminating the need for Zink for graphics API call translation and significantly improving GPU utilization.
  - Only drivers relevant to the vast majority of Android devices are compiled to reduce the package size.

## Compatibility
This section only displays GPU models confirmed to be supported through actual testing by me and others, **it does not mean that other GPU models are unsupported**. You can search for your device's GPU model in the [freedreno_devices.py](https://github.com/lfdevs/mesa-for-android-container/blob/turnip-main/src/freedreno/common/freedreno_devices.py) file (for example, search for `725` for Adreno 725). If the model has a complete device definition, it is likely supported. You are welcome to share how the driver runs on your device in [Issues](https://github.com/lfdevs/mesa-for-android-container/issues), which will help us improve the table below.

|              GPU               |    OpenGL    |  OpenGL ES   |    Vulkan    |
| :----------------------------: | :----------: | :----------: | :----------: |
|         **Adreno 660**         | ✔️ Supported | ✔️ Supported | ✔️ Supported |
| **Adreno 710/720/722/730/735/740/750** | ✔️ Supported | ✔️ Supported | ✔️ Supported |
|       **Adreno 810/829/830/840**       | ✔️ Supported | ✔️ Supported | ✔️ Supported |

Experimental support (by [**whitebelyash**](https://github.com/whitebelyash)): **Adreno 825**

## Installation
This project's Releases provide two types of installation packages: one installable via the Linux distribution's package manager, and another that must be installed by direct extraction. The first type is recommended; however, if you need the latest Mesa features (such as **Adreno 8xx support**), use the second type.

If the Turnip driver from the standard release (whose release title doesn't have the `turnip-` prefix) fails to work properly, you can use the **unpatched Turnip driver** (whose release title does have the `turnip-` prefix) by directly installing it over the standard one.

**PS:** The term "unpatched" mentioned in this project refers to the original patches from xMeM not being applied; however, some necessary patches (such as those for additional GPU support) will still be applied.

### Using the Package Manager
Depending on your Linux distribution, go to [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) and download all corresponding packages for a specific release, then follow the installation instructions provided in the release notes. Below are the latest releases for some popular Linux distributions:

| Linux Distribution |                                                            Latest Release                                                            |                                                           Unpatched Turnip driver                                                           |
| :----------------: | :----------------------------------------------------------------------------------------------------------------------------------: | :-----------------------------------------------------------------------------------------------------------------------------------------: |
|     Debian 13      |            [25.0.7-2+deb13u1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/debian%2F25.0.7-2-adreno)            |            [turnip-25.0.7-2+deb13u1](https://github.com/lfdevs/mesa-for-android-container/releases/tag/debian%2F25.0.7-2-turnip)            |
|  Ubuntu 24.04 LTS  | [25.0.7-0ubuntu0.24.04.2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/import%2F25.0.7-0ubuntu0.24.04.2-adreno) | [turnip-25.0.7-0ubuntu0.24.04.2](https://github.com/lfdevs/mesa-for-android-container/releases/tag/import%2F25.0.7-0ubuntu0.24.04.2-turnip) |
|     Fedora 43      |             [25.2.7-4.fc43](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-25.2.7-4.fc43-adreno)             |               [turnip-25.2.7-4.fc43](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-25.2.7-4.fc43)                |
|     Arch Linux     |               [26.2.0-5](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-26.2.0-devel-20260709)               |              [turnip-26.2.0-5](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-26.2.0-devel-20260709)              |

### Direct Extraction

> [!NOTE]
> The `.tar.gz` installation packages in Releases can only overwrite the existing Mesa drivers. To uninstall, you must manually delete the extracted files. These packages are intended for testing purposes only.

Currently, the "Direct Extraction" way supports the following Linux distributions:

| Linux Distribution | Installation package filename suffix |
| :----------------: | :----------------------------------: |
|     Debian 13      |        `debian_trixie_arm64`         |
|  Ubuntu 24.04 LTS  |         `ubuntu_noble_arm64`         |
|    Ubuntu 25.10    |        `ubuntu_questing_arm64`        |
|  Ubuntu 26.04 LTS  |        `ubuntu_resolute_arm64`       |
|     Fedora 43      |          `fedora_43_arm64`           |
|     Fedora 44      |          `fedora_44_arm64`           |
|     Arch Linux     |          `archlinux_arm64`           |
|     Void Linux     |             `void_arm64`             |
|     Alpine 3.24    |          `alpine_3.24_arm64`         |

1. Go to [Releases](https://github.com/lfdevs/mesa-for-android-container/releases) and download the `.tar.gz` installation packages. Please note the Linux distribution suffix in the filename, such as `debian_trixie_arm64`. You can only install the package that matches your distribution.

| Standard Installation Package | Unpatched Turnip Installation Package (usually not needed) |
| :-: | :-: |
| [26.2.0-devel-20260709](https://github.com/lfdevs/mesa-for-android-container/releases/tag/mesa-26.2.0-devel-20260709) | [turnip-26.2.0-devel-20260709](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-26.2.0-devel-20260709) |

> [!NOTE]
> For Adreno 7XX & 8XX, it should no longer be necessary to install unpatched Turnip drivers now. **Patched Turnip** from the standard release should work properly.

If you need the latest possible Mesa upstream features, you can use the **Turnip weekly builds**: [turnip-weekly](https://github.com/lfdevs/mesa-for-android-container/releases/tag/turnip-weekly)

`turnip-weekly` can be used together with standard installation packages (those whose Release titles do not have the `turnip-` prefix), or it can be used alone **(usually with better compatibility)**. When used alone, you need to change the value of the environment variable `MESA_LOADER_DRIVER_OVERRIDE` from `kgsl` to `zink`.

> [!NOTE]
> `turnip-weekly` is built weekly by GitHub Actions by pulling upstream mainline code and is **released directly without testing**, so various issues may occur. These issues are usually because upstream has not yet finished developing a certain feature, and it is necessary to wait for upstream to complete the development of that feature to resolve them. Therefore, when encountering issues with `turnip-weekly`, you can switch to other versions of the driver or wait for the next week's build.

2.  Extract the installation package directly to the root directory.

```bash
sudo tar -zxvf mesa-for-android-container_26.2.0-devel-xxxxxxxx_debian_trixie_arm64.tar.gz -C /
```
3.  Refresh the dynamic linker cache.

```bash
sudo ldconfig
```

Uninstallation can be performed by referring to the following commands:

```bash
# Copy the file list output by this command
tar tf mesa-for-android-container_26.2.0-devel-xxxxxxxx_debian_trixie_arm64.tar.gz | grep -v '/$' | tr '\n' ' ' ; echo
cd /
# Replace <file-list> with the actual file list
sudo rm <file-list>
# Reinstall the distribution-maintained Mesa drivers
# Debian or Ubuntu:
sudo apt update
sudo apt install --reinstall libegl-mesa0 libgbm1 libgl1-mesa-dri libglx-mesa0 mesa-libgallium mesa-vulkan-drivers
# Fedora:
sudo dnf reinstall mesa-filesystem mesa-libglapi mesa-libgbm mesa-libEGL mesa-libGL mesa-vulkan-drivers mesa-dri-drivers mesa-libOpenCL
# Arch Linux:
sudo pacman -S mesa mesa-docs opencl-mesa vulkan-freedreno vulkan-mesa-implicit-layers vulkan-mesa-layers
```
## Usage
Specify the environment variable `MESA_LOADER_DRIVER_OVERRIDE` when running a specific program, as follows:

```bash
MESA_LOADER_DRIVER_OVERRIDE=kgsl glmark2
```

Alternatively, add it to the `/etc/environment` file so it's loaded automatically when the container starts:

```plaintext
MESA_LOADER_DRIVER_OVERRIDE=kgsl
```

> [!TIP]
> If screen tearing issues occur when running certain programs or games, you can add the following two environment variables to force-enable vertical sync:
> ```bash
> vblank_mode=3 MESA_VK_WSI_PRESENT_MODE=mailbox
> ```

## Development
If you are a developer and want to build the drivers from this project or contribute code, please refer to the [development documentation](docs/common/development.md).

## Benchmarks
Detailed test results: [benchmark-result](docs/common/benchmark-result.md)

| Device | SoC | GPU | Container Type | glmark2 | glmark2-es2 | vkmark |
| :-: | :-: | :-: | :-: | -: | -: | -: |
| Redmi K40 Pro | Snapdragon 888 | Adreno 660 | LXC | 842 | 771 | 1170 |
| Xiaomi Pad 6 Pro | Snapdragon 8+ Gen 1 | Adreno 730 | Chroot | 1360 | 1222 | 2669 |
| REDMI K80 Pro | Snapdragon 8 Elite | Adreno 830 | PRoot | 2211 | 2206 | 1153 |
| OnePlus 15 | Snapdragon 8 Elite Gen 5 | Adreno 840 | PRoot | 3574 | 3621 | Not tested |

## Acknowledgements
  - [Lucas Fryzek](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/21570): Author of the KGSL backend code for the Mesa Freedreno driver.
  - [xMeM](https://github.com/xMeM/termux-packages/commit/401982b8d9eaef70669762bfff2a963341c65e52): For porting the Freedreno driver's KGSL backend to Termux:X11.
  - [Robert Kirkman](https://github.com/robertkirkman/termux-packages/commit/06a959eeddf153cebd0d3ea1a6c2eb2921b5f786): For integrating and improving xMeM's patches.
  - [Rob Clark](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/38450): For adding Freedreno (including Turnip) support for Adreno Gen8 architecture (including Adreno 840).
  - [whitebelyash](https://github.com/whitebelyash/mesa-tu8): Add experimental support for Adreno 825.
  - [Termux maintenance team and contributors](https://github.com/termux/termux-packages/tree/master/packages/mesa): Developed a series of patches for the normal operation of Mesa drivers on Termux.

## Star History

<a href="https://www.star-history.com/?repos=lfdevs%2Fmesa-for-android-container&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=lfdevs/mesa-for-android-container&type=date&theme=dark&legend=top-left&sealed_token=f_dmxNgIuqmmKjmrOkhU7KUe8lBmL7tzUYuGN6079kOcgw8MpydWLZzyqBVtWaGXLNp4RkqgRycjHZrfRbuiCCulhb8LrNAdqB1Y_g8_9Jpb3AKRizW4C6hWR_z5YCw58SBuirNJMIJxrlvXInHCv0omeS98I4ONf7d1OOeaWGdqKT3VsE5npYReXBC1" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=lfdevs/mesa-for-android-container&type=date&legend=top-left&sealed_token=f_dmxNgIuqmmKjmrOkhU7KUe8lBmL7tzUYuGN6079kOcgw8MpydWLZzyqBVtWaGXLNp4RkqgRycjHZrfRbuiCCulhb8LrNAdqB1Y_g8_9Jpb3AKRizW4C6hWR_z5YCw58SBuirNJMIJxrlvXInHCv0omeS98I4ONf7d1OOeaWGdqKT3VsE5npYReXBC1" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=lfdevs/mesa-for-android-container&type=date&legend=top-left&sealed_token=f_dmxNgIuqmmKjmrOkhU7KUe8lBmL7tzUYuGN6079kOcgw8MpydWLZzyqBVtWaGXLNp4RkqgRycjHZrfRbuiCCulhb8LrNAdqB1Y_g8_9Jpb3AKRizW4C6hWR_z5YCw58SBuirNJMIJxrlvXInHCv0omeS98I4ONf7d1OOeaWGdqKT3VsE5npYReXBC1" />
 </picture>
</a>
