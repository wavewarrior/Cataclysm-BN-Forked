/// The Sweep watermark: one file per platform holding the last commit that platform's sweep
/// covered, so a machine that was asleep loses nothing: the next run starts where this one stopped.
import type { Platform } from "./types.ts"

/// `.sweep/<platform>.watermark`, git-ignored: it is machine state, not repo state.
export function watermarkPath(dir: string, platform: Platform): string {
  return `${dir}/${platform}.watermark`
}

/// The file holds one sha on its first line. Anything that is not sha-shaped reads as "never
/// swept", so a corrupt or hand-edited file degrades to a full sweep rather than a wrong range.
export function parseWatermark(text: string): string | null {
  const first = text.split("\n")[0]?.trim() ?? ""
  return /^[0-9a-f]{7,40}$/.test(first) ? first : null
}

/// Whether a finished sweep moves the watermark, and to what. A run that could not happen (build
/// error, harness crash, missing binary) never advances it; a run that happened advances it to
/// HEAD even when cases failed, because those failures are the sweep's product, not its failure.
export function advanceWatermark(
  completed: boolean,
  head: string,
  current: string | null,
): string | null {
  if (!completed) return null
  if (current !== null && current.toLowerCase() === head.toLowerCase()) return null
  return head
}
