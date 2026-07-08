# Provenance: re-created from 2140-dev/bitcoin#4

This patch re-creates https://github.com/2140-dev/bitcoin/pull/4
("Remove the GUI", merged 2026-05-12, base `10ca73c02c`) on base
`bc33509ae2` (upstream master, 2026-07-06), converted to this fork's
workflow: squashed to a single self-contained `patch-01:` commit carrying
its spec, tests, and `SERIES.md` entry.

## How it was rebuilt

The four original commits were cherry-picked onto the new base and the
conflicts resolved (rung-3 style, spec as authority):

1. `b6ee6354` build: remove GUI from build system — conflicts in
   `CMakeLists.txt` (upstream moved the ENABLE_IPC/WITH_ZMQ option block
   and relocated the Qt `find_package` section; upstream also removed
   libevent entirely, so the PR-side `find_package(Libevent)` hunk was
   dropped rather than restored).
2. `93b08fbb` test: remove GUI test suite — one modify/delete conflict
   (`src/qt/test/wallettests.cpp`), resolved by deletion.
3. `ed019a8f` gui: remove src/qt and references — modify/delete conflicts
   under `src/qt/` (deleted); `doc/build-unix.md` was restructured
   upstream into a dependency table, so the GUI column and prose were
   stripped from the *new* structure instead of restoring the old one;
   `.github/ci-windows.py` resolved to HEAD (upstream had already removed
   the flags the PR edited around).
4. `77a107b8` remove remaining Qt/GUI references — 13 conflicts in CI
   scripts and `depends/`, all the same shape: upstream package lists
   evolved since May (libevent removed, pycapnp added), so each resolved
   to HEAD's current line minus GUI tokens (`qt6-*`, `libqrencode-dev`,
   `NO_QT`/`NO_QR`, armhf X11 libs), never restoring stale PR-side lines.

## Deviations from the original PR

- Upstream regressions fixed (references added after the PR's base):
  `-DBUILD_GUI=OFF` in `ci/test/00_setup_env_openbsd_cross.sh`; a
  `bitcoin-qt` mention in `doc/external-signer.md`.
- Stragglers the original PR missed, removed here:
  `contrib/completions/fish/bitcoin-qt.fish`, `doc/man/bitcoin-qt.1`,
  `bitcoin-qt` references in `contrib/completions/bash/bitcoind.bash`.
- Latent bug inherited from the original PR, fixed here: the PR gutted the
  macOS `deploy` target (GUI `.app` bundle) to an empty stub but left the
  mac CI env files requesting `GOAL="deploy"`/`"install deploy"`. That
  never fired in the original repo's CI but breaks ours with `ninja:
  error: unknown target 'deploy'`; the three mac env files are now pinned
  to `GOAL="install"`. See SPEC.md "Known upstream coupling" — a
  non-grep-able coupling only CI catches.
- Subtree boundary violation inherited from the original PR, reverted here:
  the PR edited `src/ipc/libmultiprocess/ci/scripts/bitcoin_core_ci.sh` to
  strip GUI flags, but that directory is a git subtree; `lint-subtree`
  fails ("subtree directory was touched without subtree merge"). Restored
  the file to its pristine subtree content; the stale GUI flags it passes
  our build are harmless no-ops. The sweep now excludes the subtree so a
  future rebase won't re-edit it.
- Added (required by this fork's workflow, not in the original PR):
  `patches/01-remove-gui/SPEC.md`, `patches/SERIES.md`,
  `test/functional/fork_no_gui.py` + its `test_runner.py` registration.
- Left in place, same as the original PR (inert): tsan suppressions, lint
  exclude patterns, `contrib/debian/copyright` stanzas, issue/release
  template prose.

## Rejected approach: disable instead of delete

FORK_WORKFLOW.md §2.4 prefers "disable, don't delete", and upstream's
`BUILD_GUI` already defaults to OFF — a one-line patch could pin it. That
was rejected because the point of this patch is to stop *carrying* the
GUI: its depends packages, CI time, and source tree. The cost is owning
upstream's GUI-adjacent edits forever; the sweep in SPEC.md
("Known upstream coupling") is the mitigation.
