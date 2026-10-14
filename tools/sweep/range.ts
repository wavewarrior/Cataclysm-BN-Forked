/// The commit range a sweep covers: everything the watermark has not. Pure formatting; the git
/// calls live in main.ts, so the wording an issue body gets is unit-testable.
import type { CommitRange } from "./types.ts"

/// How much history a sweep covers when there is no usable watermark, and how many subjects fit in
/// an issue body.
export const FALLBACK_COMMITS = 40
const SUBJECTS_SHOWN = 20

/// `git log --format='%h %s'` output, newest first, rendered for an issue body. `contiguous` false
/// means the watermark is not an ancestor of HEAD (a rebased or rewritten branch), so the sweep
/// says plainly that it fell back to recent history instead of pretending to know the gap.
export function renderRange(
  subjects: readonly string[],
  total: number,
  from: string | null,
  head: string,
  contiguous: boolean,
): CommitRange {
  const shown = subjects.slice(0, SUBJECTS_SHOWN)
  const hidden = total - shown.length
  const lines = hidden > 0 ? [...shown, `...and ${hidden} more`] : [...shown]
  const plural = total === 1 ? "" : "s"
  const label = from === null
    ? `${head.slice(0, 10)} with no watermark: the ${total} most recent commit${plural}`
    : contiguous
    ? `${from.slice(0, 10)}..${head.slice(0, 10)} (${total} commit${plural})`
    : `${head.slice(0, 10)} plus the ${total} most recent commit${plural} (${
      from.slice(0, 10)
    } is not an ancestor of HEAD)`
  return { label, lines, total, contiguous }
}
