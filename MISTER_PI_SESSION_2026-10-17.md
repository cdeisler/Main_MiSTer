# MiSTer Pi Session Notes - 2026-10-17

## Access and scope

- Connected to the MiSTer Pi over SSH as `root` at `192.168.68.185`.
- That address is the Ethernet connection. Ethernet remained connected during Wi-Fi setup.
- No MiSTer C/C++ source files were changed or deployed.
- Passwords are intentionally excluded from this note.

## Saved MiSTer configuration

Edited `/media/fat/MiSTer.ini` on the Pi. The latest verified saved settings are:

```ini
[MiSTer]
vga_scaler=1
fb_terminal=1
bootcore=lastcore
;bootcore_timeout=10
video_mode=1
```

- `vga_scaler=1` selects scaler output for the directly connected VGA monitor.
- The monitor was identified as a Vision Pro 19-inch, 4:3 flat panel.
- `video_mode=1` selects 1024x768 at 60 Hz, a 4:3 output mode.
- `fb_terminal=1` keeps the framebuffer console and on-screen scripts available.
- `bootcore=lastcore` enables last-core autoboot. This does not establish that every console core will restore its last ROM.
- The timeout line was commented out to remove the boot countdown.

Initially, `fb_terminal` was set to `0` to suppress the VGA framebuffer warning. This also prevented the on-screen script launcher from opening Wi-Fi setup, so it was restored to `1`.

Reboots were performed only with user authorization, including one after the latest saved display settings. No reboot was performed after the successful Wi-Fi configuration.

## Arcade and display verification

- Remotely launched `/media/fat/_Arcade/Forgotten Worlds (World, newer).mra` using MiSTer's `/dev/MiSTer_cmd` command pipe.
- Verified `/tmp/CORENAME` reported `forgottn` and `/tmp/RBFNAME` reported `JTCPS1`.
- The user confirmed the game appeared on the monitor.
- The user identified the apparent bright shading in the photograph as a camera artifact. Side borders were discussed as possible normal aspect-ratio preservation, not proof of a VGA fault.
- Subsequently loaded `/media/fat/menu.rbf`; the user confirmed the main menu appeared.

### Remaining display issue

The monitor still showed the framebuffer warning referring to `fb_terminal` and `vga_scaler`, despite the saved INI containing `fb_terminal=1` and `vga_scaler=1`.

Remote Wi-Fi setup ran on `tty2`, but its network list was not confirmed visible on the monitor. Remote keyboard attempts did not reliably navigate it. Wi-Fi was ultimately configured through its saved configuration file instead. The unused setup dialog and temporary virtual-keyboard helpers were stopped.

The framebuffer display/warning issue remains unresolved. Do not disable `fb_terminal` merely to hide it without considering the loss of on-screen script access.

### Active configuration discovery

Later read-only inspection established that the selected INI was alternate index 1, not Main. The selection marker at `0x1FFFFF04` was `0x01BA9934`. The only alternate file was `/media/fat/MiSTer_example.ini`, so index 1 maps to that file.

The selected alternate contains `vga_scaler=0`, `direct_video=0`, `fb_terminal=1`, `vscale_mode=0`, and `vscale_border=0`. Its `video_mode` line is commented out, leaving default scaler preset 0 (1280x720). These differ from the saved Main settings above and provide a plausible explanation for the warning and apparently ignored Main changes.

Verified runtime measurements:

- Linux framebuffer: 1280x720, 32 bits per pixel, stride 5120 bytes.
- Scaler header: source 384x224, stride 1280 bytes, scaled window 947x720.
- These measurements do not establish the VGA output timing: with `vga_scaler=0`, VGA is not connected to scaler output.

Remote attempts to select Main through Misc. Options did not change the marker. A controlled retry after reloading Forgotten Worlds, with explicit key presses/releases, also left index 1 selected. The historical Misc. Options layout matched the newer source; the input/menu-state failure remains undiagnosed. No selection marker was written directly, no INI files were changed during this investigation, and no reboot was performed.

Forgotten Worlds was reloaded at the end to clear uncertain menu state. Its identity and the unchanged framebuffer/scaler measurements were verified. All temporary OSD/INI virtual keyboards were removed. Game settings or physical monitor geometry were not visually verified after these attempts.

### Core settings and screenshots

The user reported Aspect Ratio Original, Scale Normal, Vertical Crop Disabled, crop offset 0, and a scan-doubler setting interpreted as Scandoubler FX None. These are a suitable baseline; no intentional game-specific display changes were made.

Two native game screenshots were retrieved: [first capture](forgottn-screen-2026-10-17.png) and [latest capture](forgottn-screen-2026-10-17-latest.png). They omit the OSD and physical monitor borders. The user observed a previously larger right border; border geometry should not be dismissed as necessarily normal without comparing the actual VGA output and monitor framing.

A complete Linux framebuffer read of 3,686,400 bytes succeeded. Such a dump captures the Linux framebuffer, not a universal combined game/OSD/monitor image. VGA capture is needed to inspect the combined output signal; a camera is still needed for monitor-created margins.

### Follow-up on 2026-10-18

The user confirmed the display looks much better and accepted the display tweaks as feature complete. A fresh read-only SSH check found Forgotten Worlds running, a 1024x768 Linux framebuffer, and an INI marker of zero. Zero is not a valid alternate-selection marker; the inspected source defaults to Main in that case. The earlier alternate-profile finding above is historical, not the latest device state.

Saved audio settings read during this check:

- `/media/fat/config/Volume.dat`: byte `01`, global attenuation index 1, not muted. Modified 2026-10-18 18:11:19 UTC.
- `/media/fat/config/forgottn_volume.cfg`: byte `06`, core attenuation index 6 (maximum supported core attenuation, not maximum loudness). Modified 2026-10-18 18:09:52 UTC.
- `/media/fat/config/forgottn_afilter.cfg`: enabled byte 0, empty filter path. Modified 2026-10-18 18:10:21 UTC.
- Neither inspected INI enabled an `afilter_default`; both retained `hdmi_audio_96k=0` and `direct_video=0`.

The installed-era audio source confirms attenuation index 0 means full volume and core index 6 is the quietest core setting. Core and global attenuation both affect output; a high function-key global-volume indication alone does not establish high core volume. Saved core attenuation 6 conflicts with the user's reported nearly-full core-volume display and requires checking the exact Misc. Options Core Volume row for Forgotten Worlds.

Earlier unverified Misc. Options navigation could have lowered Core Volume or Global Volume: Left on either row decreases loudness and the change is saved automatically. No before-value was captured, so attribution is not proven. No deliberate audio-setting edits were made during the display investigation, and this follow-up changed no remote settings. Before raising both MiSTer levels to full, reduce amplifier volume to avoid a sudden loud output.

## Samba

- Confirmed `smbd` and `nmbd` were running.
- TCP ports 445 and 139 were listening on all IPv4 interfaces.
- The `sdcard` share maps to `/media/fat`.
- Ethernet share path: `\\192.168.68.185\sdcard`.
- Wi-Fi share path using the current lease: `\\192.168.68.174\sdcard`.
- Samba configuration was not changed. Access from Windows over Wi-Fi was not separately tested.

## Wi-Fi diagnosis and setup

Initially, Linux detected only the USB hub and Microsoft keyboard/mouse receiver, with no wireless adapter or interface.

After the user reconnected the Wi-Fi dongle:

- USB detection identified a Realtek 802.11ac adapter, ID `0bda:c820`.
- The `rtl8821cu` driver loaded and created `wlan0`.
- The adapter was detected even though its blue indicator was not illuminated.
- It was initially unassociated and had a link-local address, not a router-assigned lease.

The existing `/media/fat/linux/_wpa_supplicant.conf` was not used by startup. `/etc/network/interfaces` explicitly requires `/media/fat/linux/wpa_supplicant.conf`, without the leading underscore.

The user added the `DecoMesh` credentials locally and renamed the file to the required name. Its contents and password were not displayed through the assistant's tools.

Activated Wi-Fi without rebooting:

```sh
ifup --force wlan0
dhcpcd -n wlan0
```

This system's `ifup` supports `--force`, not the attempted `-f` shorthand.

### Verified results

- Connected SSID: `DecoMesh`.
- Frequency: 5180 MHz (5 GHz).
- Reported signal at the time of testing: -25 dBm.
- Wi-Fi IPv4 address: `192.168.68.174/24`.
- Router: `192.168.68.1`.
- `ping -I wlan0 -c 3 -W 2 192.168.68.1` returned all three replies with 0% packet loss. Binding to `wlan0` verified Wi-Fi rather than Ethernet connectivity.

The configuration is saved at the path used by startup, but automatic reconnection after a reboot has not yet been tested. DHCP addresses may change.

## Local backup

A byte-for-byte copy of an earlier remote INI was saved as [MiSTer-pi-192.168.68.185.ini](MiSTer-pi-192.168.68.185.ini). The repository's original [MiSTer.ini](MiSTer.ini) was not overwritten.

The backup predates the final framebuffer and resolution changes: it still has `fb_terminal=0` and a commented `video_mode=0`. It is not a copy of the latest remote configuration.

## Follow-up

- Test automatic Wi-Fi reconnection on a future user-authorized reboot.
- Display tweaks were accepted as complete on 2026-10-18; no further display changes are requested.
- Check Forgotten Worlds' Misc. Options Core Volume against saved attenuation index 6; establish full core/global levels with amplifier volume lowered first.
- Refresh the local INI backup before treating it as the current device configuration.
- Verify last-core autoboot behavior separately; arbitrary last-ROM restoration was not established.

## Offline kiosk deployment on 2026-10-18

The user authorized backups and deployment through Samba. No restart, core load, or reboot was requested during this deployment.

- Backup directory: `D:\Documents\MiSTer-backups\192.168.68.185\2026-10-18-kiosk-predeploy`.
- Immutable original backups include `MiSTer`, `.MiSTer.old`, `MiSTer.ini`, `MiSTer_example.ini`, `menu.rbf`, all 12 saved configuration files, and `linux.img`. Copies were hash-verified against the share.
- `kiosk-staging\MiSTer.ini` inside the backup directory is the modified deployment copy, not the original INI backup.
- Built with `HTTP_SERVER=0 KIOSK_MODE=1` and deployed as `/media/fat/MiSTer-kiosk` (1,096,828 bytes). Its hash matched the local build. The original `/media/fat/MiSTer` remains unchanged.
- Main INI changed only by adding `main=MiSTer-kiosk` and replacing `bootcore=lastcore` with `bootcore=Forgotten Worlds (World, newer).mra`.
- Readback retained `vga_scaler=1`, `fb_terminal=1`, and `video_mode=1`. Original executables, alternate INI, menu core, and all saved settings still matched their backups after deployment.
- Confirmed the target MRA, its `jtcps1` core, and `games/mame/forgottn.zip` exist on the share.
- Both kiosk and normal profiles built successfully. The offline ELF contains no `http_server_` symbols. Compared with the original executable, it additionally requires `libdl.so.2` and a `GLIBC_2.8` symbol version. Read-only inspection of the backed-up Linux filesystem found glibc 2.31 and `libdl.so.2`; this is not a runtime execution test.

The deployed INI selects the kiosk executable at a subsequent startup or configuration reload. Fresh active-INI and process verification remain pending; the latest earlier measurement selected Main. Explicit maintenance-menu access remains available. Kiosk presentation does not disable Samba, SSH, the local command pipe, or Linux services.

Next: obtain restart authorization and observe the physical monitor during startup, then test a full cold boot. The binary suppresses software menus and loading overlays, but initial FPGA imagery and Linux console visibility are not yet verified.

Rollback: restore the immutable original `MiSTer.ini` from the backup directory to the Samba share, then restart only with authorization. The original executable is already intact; the unused kiosk executable need not be deleted.

### Authorized reboot verification

The user subsequently authorized one reboot. SSH used the saved local credentials and existing known-hosts file with unknown host keys rejected. Before rebooting, verified Main INI was selected, the original executable was running Forgotten Worlds, and the deployed kiosk executable was executable and hash-identical to the local build. Ran `sync` before sending `reboot`.

- Boot ID changed from `a50f261b-e086-4bf2-80f6-913b305022bd` to `a6ffb62e-4872-431d-826e-d79a78816390`.
- After reconnection, PID 568 was executing `/media/fat/MiSTer-kiosk`.
- Runtime identity reported `forgottn` and `JTCPS1`.
- Main INI selection marker remained `0x00000000`.
- Startup log confirmed `features http=0 kiosk=1`, loaded the exact Forgotten Worlds MRA and `/media/fat/_Arcade/cores/jtcps1.rbf`, and completed `user_io_init`.
- Saved Main configuration still retained the kiosk executable, fixed MRA target, `vga_scaler=1`, `fb_terminal=1`, and `video_mode=1`.

The first SSH reconnect timed out while the Pi booted; the next read-only attempt succeeded. No second reboot was sent. The user was unavailable to report the monitor sequence. Runtime autoboot is verified, but absence of visible menu/console/loading screens and cold-power-on behavior remain unverified. Temporary local SSH helper and task were removed after verification; credentials and known-hosts files were not changed.

### Monitor observation after reboot

The user subsequently confirmed Forgotten Worlds was running and reported MiSTer's intentional static/noise visual effect during startup. This is a presentation issue, not a reported signal fault. The startup was therefore not visually clean, despite successful game autoboot.

Source inspection found `user_io_init()` calls `bootcore_init()` before `video_init()` and before menu-background initialization. `video_menu_bg()` mode 0 leaves the menu core's own output visible; mode 7 draws a black software framebuffer after video initialization. Disabling the software logo does not select that black background. An application-side black background could cover later menu-core frames, but cannot guarantee suppression of output before the application starts. Removing the complete effect requires further initial menu-core/boot-stage investigation and a witnessed startup test. No boot-layer changes or additional reboot were made during this investigation.