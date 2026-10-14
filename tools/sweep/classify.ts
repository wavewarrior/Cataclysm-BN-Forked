/// Which platform failures are news. A sweep failure is PLATFORM-ONLY when Linux did not see it
/// and Quarantine does not excuse it: that is the divergence a sweep exists to catch (ADR-0005).
/// Everything else is sorted so the run's summary can say what the failures were, rather than
/// dropping them.
import { findEntry, isExpired } from "./quarantine.ts"
import type { CaseFailure, QuarantineEntry } from "./types.ts"

export type Verdict =
  /// Not in Quarantine, Linux passed it: open or update an issue.
  | "platform-only"
  /// In Quarantine and inside its window: excused, mentioned in the summary only.
  | "quarantined"
  /// Past its Quarantine deadline and still failing here: the summary calls it out.
  | "expired-quarantine"
  /// Linux fails it too: a cross-platform failure nobody has quarantined. Not the sweep's bug to
  /// file, but it must not vanish either, so the summary lists it.
  | "fails-everywhere"

export type Classified = { failure: CaseFailure; verdict: Verdict; entry?: QuarantineEntry }

export function classify(
  failures: readonly CaseFailure[],
  linuxFailures: ReadonlySet<string>,
  quarantine: readonly QuarantineEntry[],
  today: string,
): Classified[] {
  return failures.map((failure) => {
    const entry = findEntry(quarantine, failure.name)
    if (entry !== undefined && !isExpired(entry, today)) {
      return { failure, verdict: "quarantined" as const, entry }
    }
    if (linuxFailures.has(failure.name)) return { failure, verdict: "fails-everywhere" as const }
    if (entry !== undefined) return { failure, verdict: "expired-quarantine" as const, entry }
    return { failure, verdict: "platform-only" as const }
  })
}

export function selected(verdicts: readonly Classified[], verdict: Verdict): Classified[] {
  return verdicts.filter((c) => c.verdict === verdict)
}

/// Failing case names in a Linux `check` test log, in whichever of the two layouts it uses: the
/// fork runner's `FAILED: <name>` blocks (tests/fork_runner.cpp `describe`) and the Catch2 console
/// layout, where a case name between two ruler lines precedes its `FAILED:` line. Lines saying a
/// failure was NOT REPRODUCED or a CPU-path difference are the fork runner's own exoneration of a
/// case, so those names are not counted as Linux failures.
export function parseLinuxFailures(log: string): Set<string> {
  const failed = new Set<string>()
  const exonerated = new Set<string>()
  const lines = log.split("\n")
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].trimEnd()
    const forked = /^FAILED: (.+)$/.exec(line)
    if (forked !== null) {
      failed.add(forked[1].trim())
      continue
    }
    // Catch2's console layout: ruler, case name, ruler, then `file(line): FAILED:`.
    if (
      /^-{7,}$/.test(lines[i]) && /^-{7,}$/.test(lines[i + 2] ?? "") &&
      /FAILED:/.test(lines[i + 3] ?? "")
    ) {
      failed.add(lines[i + 1].trim())
      continue
    }
    const cleared = /NOT REPRODUCED.*\): (.+)$|CPU-path difference.*\): (.+)$/.exec(line)
    if (cleared !== null) exonerated.add((cleared[1] ?? cleared[2]).trim())
  }
  for (const name of exonerated) failed.delete(name)
  return failed
}
