/**
 * `compare`: the frame-equivalence gate, counting the pixels that changed outside an A/A noise
 * mask. It replaces the throwaway Python reference script and reuses this tool's own frame decoder
 * (frames.ts), so there is no second image toolchain.
 *
 * The metric is deliberately not the whole-frame mean absolute difference (`frameDelta`): a mean
 * averages a local regression away. Here a pixel counts as changed when any channel differs at all,
 * and only pixels outside the noise mask count. The mask is built from the frames of an unchanged
 * state (three or more, ideally from two launches): every pixel any pair of them disagrees on, grown
 * by `dilate` steps in each direction, so the toggling strip and animated pixels are excused while a
 * real change nearby is still caught.
 *
 * Each frame under test is compared with every base frame and its best match (fewest changed pixels
 * outside the mask) is the one reported: which base frame a test frame happens to agree with is not
 * something the caller should have to know.
 *
 * Frames of different sizes are refused by name, never resized or cropped.
 */
import { decodeFrame, type Frame } from "./frames.ts"

/** A frame this operation cannot use, or an input set it will not build a comparison from. */
export class CompareError extends Error {}

/** Pixels of one test frame that changed outside the mask. */
export type ChangedPixels = {
  changed_px: number
  /** `changed_px` as a percentage of the frame's pixels. */
  pct: number
  /** Largest single-channel difference among the changed pixels (0 when none changed). */
  max_delta: number
  /** `[left, top, right, bottom]` of the changed pixels, inclusive, or null when none changed. */
  bbox: [number, number, number, number] | null
}

/** One test frame's verdict, naming the base frame it matched best. */
export type TestVerdict = ChangedPixels & { file: string; matched_base: string }

/** The whole comparison: the mask that excuses noise, each test frame, and the verdict. */
export type Comparison = {
  verdict: "pass" | "fail"
  /** Verdict as an exit code: 0 pass, 1 fail. */
  exit_code: 0 | 1
  /** A test frame fails at more than this many changed pixels. */
  max_changed: number
  mask: { frames: number; px: number; pct: number }
  tests: TestVerdict[]
  /** The test frame with the most changed pixels: what decided the verdict. */
  worst: TestVerdict
}

/** The default gate: about 0.04% of a 2560x1440 frame, above the measured A/A floor. */
export const DEFAULT_MAX_CHANGED = 1500

/** How far the mask is grown, in pixels per direction. */
export const DEFAULT_DILATE = 2

/** At least this many same-state frames before a mask is trustworthy. */
export const MIN_MASK_FRAMES = 3

/** Per-channel difference of two same-size frames, one byte per pixel (0 identical). */
function channelDelta(a: Frame, b: Frame): Uint8Array {
  const out = new Uint8Array(a.rgb.length / 3)
  for (let i = 0, p = 0; i < a.rgb.length; i += 3, p++) {
    out[p] = Math.max(
      Math.abs(a.rgb[i] - b.rgb[i]),
      Math.abs(a.rgb[i + 1] - b.rgb[i + 1]),
      Math.abs(a.rgb[i + 2] - b.rgb[i + 2]),
    )
  }
  return out
}

/** Every pixel of `grown` that is set, or is within `steps` pixels of one that is. */
function dilate(grown: Uint8Array, width: number, height: number, steps: number): Uint8Array {
  for (let s = 0; s < steps; s++) {
    const next = Uint8Array.from(grown)
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const at = y * width + x
        if (grown[at] !== 0) continue
        if (
          (x > 0 && grown[at - 1] !== 0) || (x + 1 < width && grown[at + 1] !== 0) ||
          (y > 0 && grown[at - width] !== 0) ||
          (y + 1 < height && grown[at + width] !== 0)
        ) {
          next[at] = 1
        }
      }
    }
    grown = next
  }
  return grown
}

/** Refuses a comparison of frames that do not all have the same size. */
function assertSameSize(frames: Frame[], label: string): void {
  const [first, ...rest] = frames
  for (const f of rest) {
    if (f.width !== first.width || f.height !== first.height) {
      throw new CompareError(
        `${label}: frames of different sizes are never compared; ` +
          `${first.width}x${first.height} and ${f.width}x${f.height}`,
      )
    }
  }
}

/**
 * The noise mask of `frames`: every pixel a pair of them disagrees on, dilated by `dilate` steps.
 * Refuses fewer than `MIN_MASK_FRAMES` frames and any mix of sizes.
 */
export function noiseMask(frames: Frame[], dilateSteps = DEFAULT_DILATE): Uint8Array {
  if (frames.length < MIN_MASK_FRAMES) {
    throw new CompareError(
      `a noise mask needs at least ${MIN_MASK_FRAMES} frames of the unchanged state, not ${frames.length}`,
    )
  }
  assertSameSize(frames, "the noise mask frames")
  const { width, height } = frames[0]
  let mask: Uint8Array = new Uint8Array(width * height)
  for (let i = 0; i < frames.length; i++) {
    for (let j = i + 1; j < frames.length; j++) {
      const d = channelDelta(frames[i], frames[j])
      for (let p = 0; p < d.length; p++) {
        if (d[p] > 0) mask[p] = 1
      }
    }
  }
  if (dilateSteps > 0) mask = dilate(mask, width, height, dilateSteps)
  return mask
}

/** How a test frame differs from a base frame outside `mask`. */
export function changedOutsideMask(base: Frame, test: Frame, mask: Uint8Array): ChangedPixels {
  assertSameSize([base, test], "a comparison")
  const d = channelDelta(base, test)
  let changedPx = 0
  let maxDelta = 0
  let left = -1
  let right = -1
  let top = -1
  let bottom = -1
  for (let p = 0; p < d.length; p++) {
    if (d[p] === 0 || mask[p] !== 0) continue
    changedPx++
    if (d[p] > maxDelta) maxDelta = d[p]
    const x = p % base.width
    const y = (p - x) / base.width
    if (left === -1 || x < left) left = x
    if (x > right) right = x
    if (top === -1 || y < top) top = y
    if (y > bottom) bottom = y
  }
  return {
    changed_px: changedPx,
    pct: Math.round(10000 * 100 * changedPx / d.length) / 10000,
    max_delta: changedPx === 0 ? 0 : maxDelta,
    bbox: changedPx === 0 ? null : [left, top, right, bottom],
  }
}

/**
 * Compares each of `tests` with the best-matching frame of `base` outside `mask` and folds the
 * results into one verdict: pass when no test frame exceeds `maxChanged` changed pixels.
 */
export function compareAgainstBase(
  base: { file: string; frame: Frame }[],
  tests: { file: string; frame: Frame }[],
  mask: Uint8Array,
  maxChanged: number,
): Comparison {
  if (base.length === 0) throw new CompareError("no base frames to build a noise mask from")
  if (tests.length === 0) throw new CompareError("no frames to compare")
  assertSameSize([base[0].frame, ...tests.map((t) => t.frame)], "the comparison")
  const maskPx = [...mask].filter((v) => v !== 0).length
  const pixels = base[0].frame.width * base[0].frame.height
  const verdicts: TestVerdict[] = tests.map(({ file, frame }) => {
    let best: TestVerdict | undefined
    for (const candidate of base) {
      const hit = changedOutsideMask(candidate.frame, frame, mask)
      if (best === undefined || hit.changed_px < best.changed_px) {
        best = { ...hit, file, matched_base: candidate.file }
      }
    }
    return best!
  })
  const worst = verdicts.reduce((a, b) => b.changed_px > a.changed_px ? b : a)
  const failed = worst.changed_px > maxChanged
  return {
    verdict: failed ? "fail" : "pass",
    exit_code: failed ? 1 : 0,
    max_changed: maxChanged,
    mask: {
      frames: base.length,
      px: maskPx,
      pct: Math.round(10000 * 100 * maskPx / pixels) / 10000,
    },
    tests: verdicts,
    worst,
  }
}

/** Decodes every path of `paths` as a frame, refusing a non-image or unreadable file by name. */
export async function decodeFiles(paths: string[]): Promise<Frame[]> {
  const out: Frame[] = []
  for (const path of paths) {
    try {
      out.push(await decodeFrame(await Deno.readFile(path)))
    } catch (e) {
      throw new CompareError(`${path}: ${(e as Error).message}`)
    }
  }
  return out
}

/**
 * The `compare` operation: `base` are the frames of the unchanged state (they build the noise mask),
 * `test` the frames of the state under examination. Both are explicit files, never a directory: an
 * Episode's `captures/` mixes states, and a mask built from all of it would excuse any change.
 */
export async function runCompare(input: {
  base: string[]
  test: string[]
  max_changed: number
  dilate: number
}): Promise<Comparison> {
  const baseFrames = await decodeFiles(input.base)
  const testFrames = await decodeFiles(input.test)
  return compareAgainstBase(
    input.base.map((file, i) => ({ file, frame: baseFrames[i] })),
    input.test.map((file, i) => ({ file, frame: testFrames[i] })),
    noiseMask(baseFrames, input.dilate),
    input.max_changed,
  )
}
