# Main_MiSTer Main Binary and Wiki Repo

This repo serves as the home for the MiSTer Main binaries and the Wiki.

For the purposes of getting google to crawl the wiki, here's a link to the (not for humans) [crawlable wiki](https://github-wiki-see.page/m/MiSTer-devel/Wiki_MiSTer/wiki)

If you're a human looking for the wiki, that's [here](https://github.com/MiSTer-devel/Wiki_MiSTer/wiki)

To compile this application, read more about that [here](https://mister-devel.github.io/MkDocs_MiSTer/developer/mistercompile/#general-prerequisites-for-arm-cross-compiling)

## Custom Build Profiles

Build with Bash in WSL using the bundled ARM Linux cross-compiler:

```bash
source ./setup_default_toolchain.sh
make HTTP_SERVER=0 KIOSK_MODE=1
```

From PowerShell, the local kiosk build and binary validation helper is:

```powershell
wsl -d Ubuntu-24.04 -- bash -lc 'cd /mnt/d/Documents/GitHub/Main_MiSTer; bash .copilot/build_kiosk_validate.sh -j2'
```

The output is `bin/MiSTer`; `bin/MiSTer.elf` retains symbols for inspection.
This helper only builds and validates locally. It does not deploy or reboot.
Do not use `build.sh` for a local-only build: that legacy script also deploys.

| Flag | Default | Effect |
| --- | --- | --- |
| `HTTP_SERVER` | `1` | Set to `0` to exclude the HTTP server and its startup/poll hooks, including HTTP game launch, inventory, diagnostics, startup-game persistence, and restart endpoints. |
| `KIOSK_MODE` | `0` | Set to `1` to suppress automatic core browsing, loading progress, informational OSD overlays, framebuffer warnings, software boot/logo displays, and the autoboot countdown. Selects a black menu background before autoboot and keeps menu backgrounds and screensavers black. Explicit menu requests remain available for maintenance. |

Both flags accept only `0` or `1` and are independent. Normal launcher builds
use `make HTTP_SERVER=1 KIOSK_MODE=0`. Changing flags updates a generated feature
header and rebuilds objects; stale HTTP objects are not linked into offline builds.
`BUILDDIR` can select a separate output directory, for example
`make BUILDDIR=bin/http-validation HTTP_SERVER=1 KIOSK_MODE=0`.

Kiosk mode does not choose a game. Set the following in the device's active
MiSTer INI, preserving its existing display and controller configuration:

```ini
[MiSTer]
bootcore=Forgotten Worlds (World, newer).mra
```

The MRA must exist in a directory searched by native autoboot, such as `_Arcade`,
and its referenced RBF and ROM files must be installed. Kiosk builds force the
autoboot countdown to zero even if the INI specifies a timeout. Do not also use
the HTTP startup-game mechanism for this profile: it launches after 15 seconds.

Suppressed informational messages are retained in the runtime log
`/media/fat/mister-http-startup.log` and stderr. Kiosk mode is a presentation
profile, not an access-control boundary: local command-pipe control, maintenance
menus, SSH, Samba, and other Linux services are not disabled. An OSD unlock
sequence can be configured separately using `osd_lock`.

The executable cannot guarantee that the FPGA's initial menu-core image or Linux
boot-console output is never visible. Validate a cold boot on the target device
before calling the complete boot experience menu-free. A missing or invalid game
does not fall back to automatic browsing in kiosk mode; use maintenance access
and the runtime log to recover.

The menu core generates its own static/noise video while no framebuffer covers
it. Kiosk mode initializes the existing black framebuffer before the native
autoboot search, instead of waiting until after game selection. The startup log
records `kiosk menu background=black before autoboot`. This covers the menu core
once application video initialization completes, not frames emitted before the
application starts. Eliminating that earlier interval requires a separate
boot-stage or menu-bitstream change; this profile does not replace `menu.rbf`.
