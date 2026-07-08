# Patch series

Applied in this order on top of the base commit, one `patch-NN:` commit
each (FORK_WORKFLOW.md §1).

| # | Patch | Summary |
|---|---|---|
| 00 | [00-fork-conventions](00-fork-conventions/SPEC.md) | Series conventions: the runner accepts the `fork_*.py` contract-test namespace |
| 01 | [01-remove-gui](01-remove-gui/SPEC.md) | Remove the Qt GUI (bitcoin-qt), its tests, depends packages, and all references |
