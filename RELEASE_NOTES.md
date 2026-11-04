# gbaplanner 11.0

- **Keys are read the normal way.** The VBlank key queue (a copy of
  gbareader's "every press counts" queue, in the app since its first
  version) is gone: the keys are read once a frame after
  `bn::core::update` (Butano's keypad). A key pressed and let go while the
  app is busy (the SD check, loading a large calendar.txt, a save) is no
  longer replayed afterwards, so the cursor does not jump on by itself once
  the work is done. Hold and repeat timing is unchanged (D-pad: first repeat
  after 16 frames, then every 8, after 2 s every 6; L / R: a year after 1 s,
  then faster), and hold timers still count real frames when a frame took
  longer. Sleep works as before: L+R in the to-do list, START held 1 s
  wakes it, every key ignored after waking until all have been up a moment.
- **Large SD cards.** Checked up to the largest SDXC card (2 TiB − 8 MiB),
  exFAT with 128 KiB, 512 KiB, 1 MiB and 32 MiB clusters and FAT32 32 GiB,
  with the files at the far end of the card, on all three carts (modeled).
  - On a nearly full exFAT card the first save after each start searched
    the whole card for a free cluster from its start, bit by bit (FatFs
    keeps no free-space hint on exFAT): on a 2 TiB card the start took 84 s
    on the SuperCard model and about a minute on the Omega and EverDrive.
    Now (gbaSD 3ee9d37) the search reads the allocation bitmap a word at a
    time and starts at the app's own files (`gbasd_alloc_hint` after
    mounting, for GBAPLANNER.SYS, or /gbaplanner before the first start
    made it; it replaces the app's own hint). Measured on the modeled carts
    (SuperCard / Omega / EverDrive), every card: the first save after a
    start 9 to 18 frames (at most 0.3 s), a restart 30 to 45 frames.
  - exFAT cards with 32 MiB clusters (the largest exFAT allows) now mount;
    before, FatFs refused them.
  - A new folder's cluster is cleared 4 sectors at a time (gbaSD's
    `GBASD_FF_MULTI_BUF 4`, a 2 KiB buffer in EWRAM): the first start on a
    card with 1 MiB clusters, which makes GBAPLANNER.SYS, takes 2.3 to 5.8 s
    (was 4.3 to 6.5 s). On a card with 32 MiB clusters the very first start
    still takes minutes (it clears 32 MiB for /gbaplanner and again for
    GBAPLANNER.SYS: measured 112 s on the Omega, 162 s on the SuperCard,
    323 s on the EverDrive models); later starts take under a second.
  - A calendar.txt or todo.txt of 4 GiB or more (possible on exFAT) is
    reported as `File too large` and left unchanged; before, it was read as
    its size modulo 4 GiB, and a save would have cut it short.
- **Licence.** The UI font from gbamp3 (by Halim Jarrar) is now
  GPL-3.0-or-later like the rest of the app (it was credited as MIT).
- gbaSD 3ee9d37: fast exFAT free-space search and `gbasd_alloc_hint`, exFAT
  clusters of 32 MiB, multi-sector clearing of new folders (and its
  large-card test kit).
- Tests: a busy start with presses that must not be replayed
  (`tests/emu/planner_test.py`), and `tests/large_cards/run.py` (host part:
  4 GiB files on a 64 GiB exFAT card image; `--emu`: the exact ROM on the
  large cards, 1 MiB and 32 MiB clusters too, all carts and the Omega
  kernel's IRQ hook; the first save after a start within 45 frames, a
  restart within 120, a first start within 900, and a first start on a
  32 MiB-cluster card without /gbaplanner within 36000).
