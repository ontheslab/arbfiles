# ARBFILES Beta Test Guide

Document version: `1.00`

## Purpose

`ARBFILES` is a sysop file manager door for Ami-Express. It is designed to let
you browse file areas, inspect descriptions, move files, delete files, and use
an optional trash area without leaving the BBS.

This guide is for early testers. It focuses on what to try, what to watch for,
and what to report for the current `1.0.4.92` beta build.

## Warning

Use `ARBFILES` at your own risk.

No guarantee is made that it is safe for your data, listings, setup, or
system. It is still a beta test build and should be treated accordingly.

In short: it may break your system, your setup, your listings, or your files.

## Before You Start

- Use a test system if possible.
- If testing on a live BBS, make a backup first.
- Confirm the door is using the right `bbs_location`.
- If you want trash support, set `trash_path` in `arbfiles.cfg`.
- If you want to check monochrome mode, toggle Ami-Express colour with `M`
  before entering the door.

## Current Key Controls

See the separate `key_chart` sheet for the compact key reference.

## Change Log

See the separate `change_log` sheet for a simple version summary.

## Main Things To Test

### 1. General Browse

- Enter the door from different conferences.
- Move between conferences with `,` and `.`
- Move between areas with `[` and `]`
- Move the file highlight with `A` and `Z`
- Open full description view with `V`
- Open help with `?`
- Confirm redraw with `R`

### 2. Large Lists And Paging

- Try a `DIR` with more than `1024` entries if you can
- Move between loaded file blocks with `{` and `}`
- Confirm the `Block:` line makes sense
- Confirm the `Selected:` line makes sense on later blocks
- Confirm the visible list, preview, and tagging stay in sync after block changes
- If you change `list_block_size`, confirm paging still behaves sensibly

### 3. Source and Destination

- Press `S` to select the current file and destination
- Change destination conference with `,` and `.`
- Change destination area with `[` and `]`
- Change destination file block with `{` and `}`
- Change destination store folder with `-` and `=`
- Return to source mode with `S`

### 4. File Moves

- Select a file in source mode
- Enter destination mode
- Choose destination conference, area, and store
- Press `M`
- Confirm the move screen is clear and accurate
- Confirm the progress screen appears
- Confirm the result screen appears
- Check that:
  - the file moved to the correct store folder
  - the destination `DIR` listing was updated
  - the source `DIR` listing was updated

### 5. Tagging And Batch Move

- Tag one file with `G`
- Tag several files in a row with `G`
- Tag all files in the loaded block with `W`
- Tag all files in the current `DIR` with `E`
- Clear tags with `C`
- Enter destination mode with tagged files selected
- Confirm `M` now acts as a batch move
- Confirm the batch confirm, progress, and result screens are clear
- Confirm tagged moves work correctly when files are in nested source folders
- Confirm tags carry across loaded blocks within the same source `DIR`
- Confirm `E` really tags the whole current `DIR`, not just the current loaded block
- If you can, try one larger batch move and note the final moved / failed count

### 6. Delete and Trash

- Press `D` on a normal file
- If `trash_path` is set:
  - choose trash
  - confirm the file goes to trash
  - confirm the trash listing updates
- Try restoring a file out of trash using the normal move workflow
- Try a permanent delete

### 7. Trash Recovery

- Check normal deleted files in trash
- Check stray files physically present in the trash folder
- Confirm stray files appear even if they are not in trash `DIR1`
- Move a stray trash file back into a real area
- Confirm a generic recovery description is created

### 8. Colour

- Test with Ami-Express colour on
- Test with Ami-Express colour off
- Confirm the door obeys the BBS colour preference
- Check readability on both local and remote terminals

## Hold Area

`Hold/Held` support exists, but it has not had as much live testing because
hold files are uncommon and I have none on my systems. If you find real hold
files, please test:

- entering hold with `H`
- returning from hold with `H`
- viewing hold entries
- moving a hold entry into a normal area

## What To Report

Please note:

- exact key sequence used
- conference and area involved
- source and destination store paths if relevant
- whether colour was on or off
- whether this was local login or remote terminal
- what you expected
- what actually happened
- include the debug log if `debug_enabled=1`

If possible, please send the debug log with every issue report, even if the
problem seems obvious from a screenshot.

Screenshots are especially useful for:

- layout issues
- colour issues
- wrong labels
- wrapping or corruption
- confirmation screens
- paging/status confusion

## Known Behaviour Notes

- Ami-Express allows different layouts, so listing areas and store folders are
  not always the same thing.
- In some Ami-Express setups, the first listed download path is also the
  upload path. When that happens, that shared upload folder behaves like the
  last `DIR` area, not the first one.
- `ARBFILES` works from the populated download paths in order. It does not
  assume that raw `DLPATH.1`, `DLPATH.2`, and so on always match `DIR1`,
  `DIR2`, and so on.
- `[` and `]` change listing area.
- `{` and `}` change loaded file block.
- `-` and `=` change the destination store folder only.
- tagging now carries across loaded blocks within the same source `DIR`.
- `E` tags the whole current `DIR`, not just the current loaded block.
- Trash uses a single `DIR1` listing inside the configured trash folder.
- Stray trash files can be recovered even if they were not originally deleted
  by `ARBFILES`.
- `ARBFILES` uses `icon.library` first and then a hardened raw fallback for
  messy real-world `.info` files.

## Current Focus

The main beta focus right now is:

- browse reliability
- move reliability
- delete/trash reliability
- trash restore reliability
- block paging reliability
- tagging and batch move reliability
- configurable block-size reliability
- colour polish
- layout polish
