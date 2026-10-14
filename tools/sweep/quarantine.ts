/// Reads `tests/quarantine.h` without a compiler: the entries are a constexpr initializer list of
/// designated initializers, so one regular expression over the source is enough to know which case
/// names are excused and until when. Keeping the C++ as the single source of truth means the sweep
/// and the fork runner can never disagree about who is quarantined.
import type { QuarantineEntry } from "./types.ts"

const ENTRY =
  /\{\s*\.name\s*=\s*"((?:[^"\\]|\\.)*)"\s*,\s*\.expires\s*=\s*"((?:[^"\\]|\\.)*)"\s*,\s*\.reason\s*=\s*"((?:[^"\\]|\\.)*)"\s*,?\s*\}/g

const unescape = (text: string): string => text.replace(/\\(["\\])/g, "$1")

/// Every `quarantine_entries` element in source order.
export function parseQuarantine(source: string): QuarantineEntry[] {
  const entries: QuarantineEntry[] = []
  for (const match of source.matchAll(ENTRY)) {
    entries.push({
      name: unescape(match[1]),
      expires: unescape(match[2]),
      reason: unescape(match[3]),
    })
  }
  return entries
}

/// ISO dates order as strings, which is what `split_by_quarantine` relies on: an entry is past its
/// deadline when `expires` sorts before today.
export function isExpired(entry: QuarantineEntry, today: string): boolean {
  return entry.expires < today
}

export function findEntry(
  entries: readonly QuarantineEntry[],
  name: string,
): QuarantineEntry | undefined {
  return entries.find((entry) => entry.name === name)
}

/// Today in UTC, ISO formatted: the same instant the C++ `today_iso()` uses.
export function todayIso(now: Date = new Date()): string {
  return now.toISOString().slice(0, 10)
}
