---
id: 00-fork-conventions
title: Fork series conventions (fork_ functional-test namespace)
status: active
consensus: false
mechanical: false
owner: janb84
since: 2026-07-07
---

## Intent

Establish the conventions the whole series depends on, which is why this
patch precedes all others and must outlive any of them — retiring another
patch never removes them. Two things:

1. The `fork_` namespace for the series' contract tests: the functional test
   runner accepts test scripts named `fork_*.py`. Every patch in the series
   ships its contract tests under this namespace (FORK_WORKFLOW.md §2.6).
2. Per-commit CI configures without GUI dependencies. The fork's CI image
   installs no Qt, but every commit *before* the GUI removal still carries
   `src/qt` and a `dev-mode` preset that asks for it, so per-commit CI must
   pass the GUI off explicitly.

## Invariants (must hold on every base, verified by tests)

- Test scripts named `fork_*.py` pass the runner's naming-convention check
  (`check_script_prefixes`).
- Purely additive: the set of upstream-accepted prefixes is unchanged.
- Every commit in the series configures with the CI's per-commit build
  command on an image with no Qt, GUI removed or not.

## Touchpoints

- test/functional/fork_conventions.py   (new file)
- test/functional/test_runner.py        (one word added to the accepted-prefix
                                         regex in check_script_prefixes();
                                         one registration line for the
                                         contract test)
- .github/ci-test-each-commit-exec.py   (-DBUILD_GUI=OFF, -DBUILD_GUI_TESTS=OFF
                                         and -DWITH_QRENCODE=OFF ahead of
                                         --preset=dev-mode)

## Tests (the contract)

- test/functional/fork_conventions.py
  - the runner's naming check accepts a `fork_`-prefixed script name
  - this contract test itself is registered with the runner
- The per-commit CI invariant is currently checked only by CI itself
  (`.github/workflows/ci.yml`'s `test-each-commit` job), not by a contract
  test. See the note below.

## Known upstream coupling

Anchors to the accepted-prefix regex inside `check_script_prefixes()` in
`test/functional/test_runner.py`. If upstream renames, moves, or
restructures that check (e.g. into `test/lint`), re-add `fork` wherever the
accepted name prefixes end up being defined. If upstream ever introduces its
own conflicting `fork_` test prefix, rename our namespace series-wide
(mechanical rename plus spec/test updates in every patch).

The GUI-off flags anchor to the `cmake` argument list in
`.github/ci-test-each-commit-exec.py`. They must sit ahead of
`--preset=dev-mode` only for readability — command-line `-D` takes
precedence over a preset's `cacheVariables` either way — and they must live
in *this* patch, not in the GUI-removal patch: `git rebase --exec` runs the
script from the tree of the commit under test, so a fix that lands later in
the series is absent exactly where it is needed.

This generalizes beyond the GUI. **Any** patch that removes a build
dependency makes every earlier commit unbuildable in a CI image provisioned
from the series tip. When such a patch is added, disable the dependency for
per-commit CI here, in the same way.
