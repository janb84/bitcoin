---
id: 01-remove-gui
title: Remove the Qt GUI (bitcoin-qt)
status: active
consensus: false
mechanical: false
owner: janb84
since: 2026-07-06
---

## Intent

This fork ships headless nodes only. Remove the Qt GUI entirely: the
`bitcoin-qt` binary, the `src/qt/` source tree, its test suite
(`test_bitcoin-qt`), the Qt/qrencode/X11 depends packages, GUI build
options, GUI CI configuration, translation tooling, and documentation
references. This shrinks build times, CI surface, and the dependency
footprint; nothing in the fork's roadmap needs a GUI.

Re-created from 2140-dev/bitcoin#4 (see `notes/provenance.md`).

## Invariants (must hold on every base, verified by tests)

- The tree contains no `src/qt/` directory.
- The build system exposes no `BUILD_GUI`, `BUILD_GUI_TESTS`,
  `WITH_QRENCODE`, or `WITH_DBUS` options; no configuration can produce a
  `bitcoin-qt` target or binary.
- `depends` carries no Qt, qrencode, or X11 packages.
- Runtime behavior of all remaining binaries (`bitcoind`, `bitcoin-cli`,
  ...) is identical to upstream: this patch only deletes GUI code and
  removes references to it; it changes no non-GUI code paths.

## Touchpoints

Unusual breadth for this series — it is a deletion patch (FORK_WORKFLOW.md
§2.4). The durable contract is the invariants above, not the file list.

- Deleted subtrees/files: `src/qt/` (incl. `src/qt/test/`), `share/qt/`,
  `src/init/bitcoin-qt.cpp`, `src/init/bitcoin-gui.cpp`,
  `cmake/module/FindQt.cmake`, `cmake/module/FindQRencode.cmake`,
  Qt/qrencode/X11 packages under `depends/packages/`,
  `contrib/macdeploy/` Qt deploy tooling, `.tx/`,
  `doc/translation_process.md`, `test/lint/lint-qt-translation.py`,
  `contrib/completions/fish/bitcoin-qt.fish`, `doc/man/bitcoin-qt.1`,
  the GUI-only Guix build scripts
  `contrib/guix/libexec/build_{linux,macos,win}_gui.sh` and their manifest
  `contrib/guix/manifest_gui.scm`.
- Reference-removal edits (no behavior change): `CMakeLists.txt`,
  `src/CMakeLists.txt`, `cmake/`, `CMakePresets.json`, `vcpkg.json`,
  `README.md`, `CONTRIBUTING.md`, `ci/test/`, `.github/`, `contrib/`,
  `depends/{Makefile,README.md,toolchain.cmake.in,packages/packages.mk}`,
  `doc/`, `share/setup.nsi.in`, `src/{bitcoin.cpp,bitcoind.cpp,
  clientversion.cpp,coins.h,interfaces/init.h,node/connection_types.h,
  node/README.md,test/README.md}`,
  `test/{README.md,lint/lint-includes.py,lint/lint-locale-dependence.py,
  lint/lint-circular-dependencies.py}`.
- NOT touched, by rule: anything under `src/ipc/libmultiprocess/` (a git
  subtree). Its CI passes stale `-DBUILD_GUI=OFF`/`NO_QT=1` flags to our
  build; those are now harmless no-ops (unused CMake vars / unknown make
  vars). Editing them would violate the subtree boundary (see below).
- Test registration: `test/functional/test_runner.py` (one line).

## Tests (the contract)

- `test/functional/fork_no_gui.py`
  - `src/qt/` is absent from the tree
  - no GUI option (`BUILD_GUI`, `WITH_QRENCODE`, `WITH_DBUS`) or
    `bitcoin-qt` reference in the CMake build system
  - no Qt/qrencode packages under `depends/packages/`
  - no `bitcoin-qt` binary in the build directory

## Known upstream coupling

A deletion patch conflicts in the *opposite* direction from an addition:
if upstream adds a new reference to the GUI (a doc sentence, a CI flag, a
new call site), the series still applies cleanly and nothing fails loudly
unless the new reference lands in a file the tests scan. On every rebase,
after replaying the series, sweep for reintroduced references:

    git grep -iE 'build_gui|bitcoin-qt|bitcoin-gui|qrencode|native_qt|with_dbus|(^|[^a-z0-9_.])qt/' \
        -- ':!depends/patches' ':!doc/release-notes' ':!patches' \
           ':!src/ipc/libmultiprocess'

and remove what upstream added (two such regressions appeared between
2026-05 and 2026-07 alone: `ci/test/00_setup_env_openbsd_cross.sh` and
`doc/external-signer.md`). The `qt/` alternative matches bare module
paths, which a narrower `src/qt` misses — that gap hid four stale
`qt/... -> qt/walletmodel -> ...` entries in
`test/lint/lint-circular-dependencies.py`'s expected list, only caught
later by the lint itself. The `bitcoin-gui`, `native_qt` and `with_dbus`
alternatives close the same class of gap for names that carry no slash:
a stale `native_qt` mention survived in `depends/README.md` until the
2026-07-27 rebase because no earlier alternative matched it.

Not everything the deletion breaks is grep-able. The macOS `deploy` build
target only ever packaged the GUI `.app` bundle;
`add_macos_deploy_target()` is now an empty stub, so no `deploy` target
exists on macOS. Any CI job that runs `GOAL="deploy"` (or `install
deploy`) there fails with `ninja: error: unknown target 'deploy'` — a
regression with no GUI keyword in it, invisible to the sweep and only
caught by actually running the mac CI jobs. The mac env files
(`ci/test/00_setup_env_mac_native.sh`, `_mac_cross.sh`,
`_mac_cross_intel.sh`) are therefore pinned to `GOAL="install"` by this
patch. The Windows `deploy` target survives (NSIS installer of the
headless binaries) and is intentionally left in the win64 env files.

Two anchors moved in upstream's 2026-07 churn and are recorded here so the
next rebase does not have to re-derive them. The Windows installer
generation left `cmake/module/GenerateSetupNsi.cmake` for
`cmake/script/GenerateWindowsInstaller.cmake.in` (bitcoin/bitcoin#35537);
the `BITCOIN_GUI_NAME` definition this patch removes lives in whichever
file currently configures `share/setup.nsi.in`. The Guix build was split
into headless and GUI scripts per host
(`contrib/guix/libexec/build_{linux,macos,win}.sh` plus `*_gui.sh`); the
GUI halves and their `manifest_gui.scm` are deleted outright and their
invocations removed from the `case "$HOST"` dispatch in
`contrib/guix/guix-build`. If upstream splits further, the rule is the
same: delete the GUI-only script, keep the headless one, and drop the
`-DBUILD_GUI*` flags it no longer needs to pass.

`src/ipc/libmultiprocess/` is a **git subtree**: `lint-subtree`
(`test/lint/git-subtree-check.sh`) fails if its directory tree diverges
from the tree pulled by the last subtree merge, so its files must never be
edited directly in this repo — only via a subtree merge from upstream.
The sweep therefore excludes it (`:!src/ipc/libmultiprocess`), and the GUI
flags its CI still passes to our build are left as harmless no-ops. If
upstream libmultiprocess ever needs those flags dropped, that is a change
to make upstream and pull in, not a hunk in this patch.

Inert references intentionally left in place:
`test/sanitizer_suppressions/tsan` suppressions, lint exclude patterns in
`test/lint/test_runner/src/lint_text_format.rs`, historical mentions in
`contrib/debian/copyright`, `.github/ISSUE_TEMPLATE/bug.yml`,
`doc/release-notes-empty-template.md`, and this patch's own contract test
`test/functional/fork_no_gui.py`.

One more is deliberate rather than historical: the
`-DBUILD_GUI=OFF -DBUILD_GUI_TESTS=OFF -DWITH_QRENCODE=OFF` flags in
`.github/ci-test-each-commit-exec.py`. They exist *because* of this patch —
per-commit CI builds each commit in the series, and the ones before this one
still carry `src/qt` on an image with no Qt — but they are owned by
patch 00-fork-conventions, which must carry them so they are present in every
commit's tree. Do not remove them here; see `patches/00-fork-conventions/SPEC.md`.
