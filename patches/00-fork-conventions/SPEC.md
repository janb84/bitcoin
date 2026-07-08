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

Establish the `fork_` namespace for the series' contract tests: the
functional test runner accepts test scripts named `fork_*.py`. Every patch
in the series ships its contract tests under this namespace
(FORK_WORKFLOW.md §2.6), so this patch precedes all others and must outlive
any of them — retiring another patch never removes the namespace.

## Invariants (must hold on every base, verified by tests)

- Test scripts named `fork_*.py` pass the runner's naming-convention check
  (`check_script_prefixes`).
- Purely additive: the set of upstream-accepted prefixes is unchanged.

## Touchpoints

- test/functional/fork_conventions.py   (new file)
- test/functional/test_runner.py        (one word added to the accepted-prefix
                                         regex in check_script_prefixes();
                                         one registration line for the
                                         contract test)

## Tests (the contract)

- test/functional/fork_conventions.py
  - the runner's naming check accepts a `fork_`-prefixed script name
  - this contract test itself is registered with the runner

## Known upstream coupling

Anchors to the accepted-prefix regex inside `check_script_prefixes()` in
`test/functional/test_runner.py`. If upstream renames, moves, or
restructures that check (e.g. into `test/lint`), re-add `fork` wherever the
accepted name prefixes end up being defined. If upstream ever introduces its
own conflicting `fork_` test prefix, rename our namespace series-wide
(mechanical rename plus spec/test updates in every patch).
