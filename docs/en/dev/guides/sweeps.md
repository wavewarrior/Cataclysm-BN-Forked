---
title: Platform sweeps
---

# Platform sweeps

Per-change verification runs on Linux only (`deno task check`). A **platform sweep** runs the whole
suite natively on Windows or macOS and files issues for failures that only that platform has. It
never blocks anything: nobody waits for it, and a missed run is covered by the next.

## What a sweep does

`deno task sweep --platform windows|macos`:

1. Reads the watermark `.sweep/<platform>.watermark` (git-ignored): the last commit a sweep fully
   covered. No watermark means the 40 most recent commits.
2. Configures and builds with the platform's preset (`osx-arm-slim`, or `win-rel`), then runs the
   whole suite in-process: `cata_test-tiles --rng-seed 1`. Windows also runs the `[render]` tier.
3. Compares the failures against Linux's (`--linux-log`, default `.sweep/linux-check.log`) and
   against `tests/quarantine.h`. A failing case that is not quarantined and does not fail on Linux
   is **platform-only**.
4. Opens or updates one issue per platform-only case, titled
   `[platform-sweep] <case> (<platform>)`, labelled `platform:<platform>`, with the commit range,
   the platform and a log excerpt. Quarantine entries past their `expires` that still fail go into
   one `[platform-sweep] summary (<platform>)` issue.
5. A run that completed (whatever it found) moves the watermark to HEAD. A run that could not
   happen (build error, harness crash) does not.

`--dry-run` prints the plan and changes nothing. `--runner fake --fake-failures a,b` rehearses a
sweep without building.

## Windows: scheduled

`tools/sweep/windows/install-task.ps1` registers a daily 03:00 task that runs
`windows-sweep.ps1`: fast-forward to `origin/feature/improvements`, skip when a build or test is
already running or the tree has local changes, then `deno task sweep --platform windows`. The task
has a two-hour limit and runs one instance at a time. Preview it with `-WhatIf`; registering it is
the owner's step.

## macOS: opt-in

Run `deno task sweep --platform macos` when you want a Metal-side check. Skipping it for any
number of commits is fine: the next run covers every commit since the watermark.

## Needs

`gh` authenticated with permission to create issues and labels on the repository.
