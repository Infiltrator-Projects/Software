<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Validation

[Architecture](ARCHITECTURE.md), [Transactions](TRANSACTIONS.md) and [State](STATE.md) define the boundaries under test. [Release](RELEASE.md) owns exact artifact qualification and publication.

## Canonical package gate

On a Debian-family development host with the build dependencies declared by `debian/control`:

```sh
git submodule update --init --recursive
python3 tests/source_contracts.py
python3 tests/hardening_contracts.py
python3 tests/documentation_contracts.py
dpkg-buildpackage -us -uc -b
```

`debian/rules` owns the release configuration and executes CTest. CI uses that package path rather than compiling a second release graph. For a focused development build, configure CMake, build the owning target and run CTest with output on failure.

The CTest regression executables retain assertions in Release builds through the test directory's compiler options. Removing those options would erase checks from the many assert-based fixtures.

## Source and engine contracts

Tests cover exact transaction specifications, package versions/dependencies/candidates, configured source precedence, phased updates, installed inventory, policy, state generations, history and compatibility. Contract checks protect ownership and authorisation boundaries across the GUI, client, engine service and privileged executor.

A retained package generation after refresh failure remains readable evidence. It is not healthy enough to authorise a new transaction. Reviewed transaction identity includes the state generation and source fingerprint; the engine remains package-state authority. UI views must not reintroduce their own APT/dpkg inventory fallback.

## Service, package and publication evidence

CI validates the built Debian package, performs engine service smoke checks, and qualifies the exact source/package bundle before publication. Release identity, allowed assets, hashes and branch state are owned by the release implementation and its documented contract.

Passing deterministic tests does not establish behaviour on every distribution or interrupted real package transaction. Host qualification must record distribution, exact source/package version, authorisation decisions, package-state generations and recovery results. Use disposable environments for mutation and repair scenarios.

## Documentation discipline

The maintained index links every Markdown contract in this directory. Documentation describes owners, invariants and failure/recovery semantics; live CI owns automatic pass/fail evidence, and implementation owns schema/version constants.
