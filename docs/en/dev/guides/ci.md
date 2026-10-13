# Continuous integration

One workflow: [`.github/workflows/ci.yml`](../../../../.github/workflows/ci.yml). It runs on a
`pull_request` into `feature/improvements` (and on `workflow_dispatch`), on a free `ubuntu-24.04`
runner, and does exactly one thing: install LLVM 22 from apt.llvm.org plus the build dependencies,
restore ccache, build the clang-tidy plugin, then run

```sh
deno task check --no-fix --base origin/feature/improvements
```

so CI runs the same gate a lane runs, on the same Linux lineage. Green CI plus a human merge is the
whole promotion path.

## What was deleted

The other fifteen workflows are gone and nothing replaces them:

- `build.yml`, `matrix.yml`, `clang-tidy.yml`, `autofix.yml`, `factory-ci.yml`, `coop-e2e.yml` —
  folded into `check`.
- `release.yml`, `manual-release.yml`, `experimental-release.yml` — releases are now manual, built
  locally and uploaded by hand.
- `docs.yml` — the doc site is built locally with `deno task docs`.
- `build-translations.yml`, `i18n-extraction.yml`, `i18n-printf-format.yml`,
  `push-translation-template.yml` — the fork is English only.
- `pr-artifact-publish.yml`, `pr-artifact-stale.yml` — no PR preview bots.

Platform coverage that CI no longer gives (Windows, macOS) comes from Platform sweeps, not from CI.
