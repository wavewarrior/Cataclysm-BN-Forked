---
name: Factory ticket
about: A unit of work for an unattended factory lane (parsed by tools/factory/ticket.ts)
title: "type(scope): short imperative summary"
labels: ["factory:draft"]
---

<!-- The title must be a conventional-commit title: it becomes the PR title.
     Add the label spec:<slug>. `deno task factory release <slug>` flips the whole spec to
     factory:ready; that is the one human approval point. Add `gameplay` or `render` when a
     bnplay Episode must verify the change. Keep these section names exactly. -->

## Goal

<!-- What changes and why, in a few sentences. -->

## Acceptance

<!-- The observable result that means this is done. -->

## Touches

<!-- Paths the change is expected to edit, one per line. Protected paths (see
     tools/factory/config.ts) are never allowed. -->

## Test tags

<!-- Catch2 tags the gate must run for this change, one per line, e.g. [rot]. -->

## Episodes

<!-- Optional. bnplay Trial files (one per line) the gate runs; required for gameplay/render. -->

## Depends on

<!-- Optional. Issue numbers that must be closed first, one per line, e.g. #12. -->

## No test needed

<!-- Optional. One line of reasoning, only when src/ changes without tests/ (otherwise the gate
     rejects the change). -->
