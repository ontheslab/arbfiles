# ARBFILES - Ami-Express File Manager Door

An Ami-Express sysop file manager door for browsing file areas, inspecting
descriptions, moving files, deleting files, and using an optional trash area.
Written in C and cross-compiled on Windows for m68k-AmigaOS.

---

## Status

| Component | Status |
|---|---|
| `arbfiles` door | Working - tested under live Ami-Express and WinUAE testing |
| Large-list paging | Working - configurable loaded blocks with paging across large `DIR` files |
| Tagging and batch move | Working - tagging across blocks in the same `DIR`, including `tag all in this DIR` |
| Trash and recovery | Working - optional trash, stray trash-file recovery, and restore support |

### Background

`ARBFILES` was built to provide a practical Ami-Express door file manager for
sysop use. The aim is to work with real Ami-Express conference layouts, not just
ideal simple setups.

That means I have tried to build it to work with:

- listing areas and store folders not always being the same thing
- rotated `DIR` to folder layouts
- nested source folders
- odd real-world `.info` data

---

## Requirements

- Ami-Express with AEDoor available
- m68k AmigaOS system for the final binary

---

## Build

Cross-compile on Windows using vbcc for m68k-AmigaOS:

```batch
build.bat
```

| Output | Source | Purpose |
|---|---|---|
| `arbfiles` | `arbfiles.c` + modules | Ami-Express door binary |

The build script requires vbcc (`vc`) on the system PATH and the local AmigaOS
include trees at:

```
C:\amiga-dev\targets\m68k-amigaos\include
C:\amiga-dev\targets\m68k-amigaos\posix\include
```

---

## Installation

Copy `arbfiles` to the Ami-Express doors location on the Amiga.
Place `arbfiles.cfg` in the same drawer (`PROGDIR:`).
Create an appropriate `.info` file in your Ami-Express commands folder to launch
it as a sysop door.

---

## Configuration

Copy `arbfiles.cfg.example` to `arbfiles.cfg` and edit as needed.

| Key | Default | Description |
|---|---|---|
| `bbs_location` | - | BBS root location used for Ami-Express config discovery |
| `trash_path` | - | Optional trash folder for delete-to-trash support |
| `allow_hold_area` | `1` | Set to `0` to hide the `Hold/Held` special area |
| `disable_paging` | `1` | Set to `1` to suppress Ami-Express paging prompts during the door session |
| `list_block_size` | `1024` | Loaded file-block size; clamped to `128..4096` |
| `debug_enabled` | `0` | Set to `1` to enable file-based debug logging |
| `debug_log` | `arbfiles.log` | Log file path (for example `RAM:arbfiles.log`) |

---

## Manuals

- [`manual/arbfiles_manual.md`](manual/arbfiles_manual.md) - full install, config, and usage reference
- [`manual/key_chart.md`](manual/key_chart.md) - compact key reference
- [`manual/change_log.md`](manual/change_log.md) - version history

Plain text versions are also in the same folder.

---

## Source Files

| File | Responsibility |
|---|---|
| `arbfiles.c` | Main Program: conference loading, browse state, move/delete workflow |
| `door_config.c/.h` | `key=value` config loader |
| `aedoor_bridge.c/.h` | AEDoor session handling, user identity, paging suppression, colour state, key polling |
| `ae_config_scan.c/.h` | Ami-Express `CONFCONFIG` and conference `.info` discovery |
| `dirlist.c/.h` | `DIR` file parsing, loaded-block paging, and entry storage |
| `tagset.c/.h` | Tag tracking across loaded blocks in the same source `DIR` |
| `file_ops.c/.h` | File moves, deletes, trash handling, and `DIR` rewrite / rollback logic |
| `ui.c/.h` | Interactive ANSI text UI, menus, confirms, help, and progress screens |
| `doorlog.c/.h` | File-based debug logging |
| `aedoor_inline.h` | Local AEDoor inline stubs |
| `door_version.h` | `ARBFILES_VERSION` string |

---

## Research References

| Reference | Used for |
|---|---|
| Ami-Express source | `DIR` handling, conference/path layout, upload/download rules, file maintenance flow |
| Ami-Express setup-tool source | `CONFCONFIG`, `DLPATH.n`, `ULPATH.n`, and conference editor storage |
| AEDoor 2.8 SDK | Door open/use/close handling, `GetDT` / `SetDT`, output, key handling, and session data |
| AmiXDoors examples | Confirmed practical AEDoor patterns such as key polling and screen handling |
| `arblink` | Shared project structure, door pattern, and local logging style |

---

## Version

`arbfiles 1.0.4.92`
