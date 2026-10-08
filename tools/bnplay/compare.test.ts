/**
 * The `compare` pixel-count gate, against frames built by hand: the mask, the changed-pixel count,
 * the bounding box, the verdict and the refusals. Every number the operation prints here is one the
 * test predicted from the frame contents.
 */
import { assert, assertEquals, assertRejects, assertThrows } from "@std/assert"
import {
  changedOutsideMask,
  compareAgainstBase,
  CompareError,
  DEFAULT_DILATE,
  MIN_MASK_FRAMES,
  noiseMask,
  runCompare,
} from "./compare.ts"
import type { Frame } from "./frames.ts"

const W = 8
const H = 6

/** A frame of one colour, or of `pixel(x, y)` per pixel. */
function frame(
  colour: [number, number, number],
  pixel?: (x: number, y: number) => [number, number, number],
): Frame {
  const rgb = new Uint8Array(W * H * 3)
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      rgb.set(pixel?.(x, y) ?? colour, 3 * (y * W + x))
    }
  }
  return { width: W, height: H, rgb }
}

/** The quiet state, except one pixel of column 0, row `parity`, that burns brighter. */
const flickering = (parity: number) =>
  frame([10, 20, 30], (x, y) => x === 0 && y === parity ? [200, 20, 30] : [10, 20, 30])

const quiet = frame([10, 20, 30])

/** A 24-bit bottom-up BMP of `f`, as the game's swapchain dump writes it. */
function bmp(f: Frame): Uint8Array {
  const stride = (f.width * 3 + 3) & ~3
  const pixels = new Uint8Array(stride * f.height)
  for (let y = 0; y < f.height; y++) {
    for (let x = 0; x < f.width; x++) {
      const at = 3 * (y * f.width + x)
      const row = stride * (f.height - 1 - y) + 3 * x
      pixels[row] = f.rgb[at + 2]
      pixels[row + 1] = f.rgb[at + 1]
      pixels[row + 2] = f.rgb[at]
    }
  }
  const header = new Uint8Array(54)
  const view = new DataView(header.buffer)
  header.set([0x42, 0x4d], 0)
  view.setUint32(2, 54 + pixels.length, true)
  view.setUint32(10, 54, true)
  view.setUint32(14, 40, true)
  view.setInt32(18, f.width, true)
  view.setInt32(22, f.height, true)
  view.setUint16(26, 1, true)
  view.setUint16(28, 24, true)
  view.setUint32(34, pixels.length, true)
  return new Uint8Array([...header, ...pixels])
}

/** Writes `frames` as BMPs of one scratch state, hands over the paths, and removes the directory. */
async function withBmps(
  prefix: string,
  frames: Frame[],
  body: (paths: string[]) => Promise<void>,
): Promise<void> {
  const dir = await Deno.makeTempDir({ prefix: "bnplay-compare-" })
  try {
    const paths = []
    for (const [i, f] of frames.entries()) {
      paths.push(`${dir}/${prefix}-${i}.bmp`)
      await Deno.writeFile(paths[i], bmp(f))
    }
    await body(paths)
  } finally {
    await Deno.remove(dir, { recursive: true })
  }
}

const BASE = [flickering(0), flickering(1), flickering(2)]
const named = (frames: Frame[]) => frames.map((f, i) => ({ file: `${i}`, frame: f }))

Deno.test("the mask covers where the base frames disagree, grown by the dilation", () => {
  const mask = noiseMask(BASE, DEFAULT_DILATE)
  // Column 0 rows 0..2 disagreed; two rounds of one-pixel growth reach rows 0..4 of column 0,
  // rows 0..3 of column 1 and rows 0..2 of column 2.
  assertEquals(mask[0 * W + 0], 1)
  assertEquals(mask[4 * W + 0], 1)
  assertEquals(mask[3 * W + 1], 1)
  assertEquals(mask[0 * W + 2], 1)
  assertEquals(mask[4 * W + 2], 0, "two columns from column 0 is beyond the growth")
  assertEquals(mask[5 * W + 0], 0, "row 5 is three pixels from the nearest disagreement")
  assertEquals(mask[0 * W + 3], 0, "column 3 is three pixels away")
  assertEquals(
    noiseMask([frame([1, 2, 3]), frame([1, 2, 3]), frame([1, 2, 3])]).some((v) => v !== 0),
    false,
    "frames that agree everywhere mask nothing",
  )
})

Deno.test("a change inside the mask is noise; outside it is counted with box and delta", () => {
  const mask = noiseMask(BASE)
  assertEquals(
    changedOutsideMask(quiet, flickering(0), mask).changed_px,
    0,
    "the flicker the mask was built from is excused",
  )
  const planted = frame(
    [10, 20, 30],
    (x, y) => x >= 5 && x <= 6 && y >= 3 && y <= 4 ? [210, 15, 30] : [10, 20, 30],
  )
  const hit = changedOutsideMask(quiet, planted, mask)
  assertEquals(hit.changed_px, 4)
  assertEquals(hit.pct, Math.round(10000 * 100 * 4 / (W * H)) / 10000)
  assertEquals(hit.max_delta, 200, "the largest single-channel difference")
  assertEquals(hit.bbox, [5, 3, 6, 4])
  assertEquals(changedOutsideMask(quiet, quiet, mask).bbox, null)
})

Deno.test("the verdict is the worst test frame against its best-matching base frame", () => {
  const mask = noiseMask(BASE)
  const planted = frame([10, 20, 30], (x, y) => x === 7 && y === 0 ? [10, 20, 130] : [10, 20, 30])
  const failing = compareAgainstBase(
    named(BASE),
    [{ file: "quiet", frame: quiet }, { file: "planted", frame: planted }],
    mask,
    0,
  )
  assertEquals(failing.verdict, "fail")
  assertEquals(failing.exit_code, 1)
  assertEquals(failing.mask.frames, 3)
  assertEquals(failing.tests[0].changed_px, 0)
  assertEquals(failing.tests[1].changed_px, 1)
  assertEquals(failing.tests[1].max_delta, 100)
  assertEquals(failing.tests[1].matched_base, "0", "the base frame it agrees with best")
  assertEquals(failing.worst.file, "planted")
  const passing = compareAgainstBase(named(BASE), [{ file: "quiet", frame: quiet }], mask, 10)
  assertEquals(passing.verdict, "pass")
  assertEquals(passing.exit_code, 0)
})

Deno.test("the operation reads BMP files and reports the numbers it predicts", async () => {
  // Right half goes blue: 4 columns x 6 rows, none of them inside the dilated mask.
  const planted = frame([10, 20, 30], (x) => x >= 4 ? [10, 20, 130] : [10, 20, 30])
  await withBmps("base", BASE, async (base) => {
    await withBmps("test", [planted], async ([test]) => {
      const failing = await runCompare({
        base,
        test: [test],
        max_changed: 23,
        dilate: DEFAULT_DILATE,
      })
      assertEquals(failing.verdict, "fail")
      assertEquals(failing.exit_code, 1)
      assertEquals(failing.worst.changed_px, 24)
      assertEquals(failing.worst.bbox, [4, 0, 7, 5])
      assertEquals(failing.worst.max_delta, 100)
      assertEquals(failing.mask.px, 12, "five, four and three pixels of columns 0, 1 and 2")
      const passing = await runCompare({
        base,
        test: [test],
        max_changed: 24,
        dilate: DEFAULT_DILATE,
      })
      assertEquals(passing.verdict, "pass")
      assertEquals(passing.exit_code, 0)
      assertEquals(passing.worst.pct, Math.round(10000 * 100 * 24 / (W * H)) / 10000)
    })
  })
})

Deno.test("a mask of fewer than three frames is refused by name", async () => {
  await withBmps("pair", [flickering(0), flickering(1)], async (base) => {
    await withBmps("test", [quiet], async ([test]) => {
      const error = await assertRejects(
        () => runCompare({ base, test: [test], max_changed: 1500, dilate: DEFAULT_DILATE }),
        CompareError,
        `at least ${MIN_MASK_FRAMES} frames of the unchanged state, not 2`,
      )
      assertEquals(
        assertThrows(() => noiseMask([quiet, quiet]), CompareError).message,
        error.message,
      )
    })
  })
})

Deno.test("frames of different sizes are refused by name", async () => {
  const odd: Frame = { width: W + 1, height: H, rgb: new Uint8Array((W + 1) * H * 3) }
  await withBmps("base", BASE, async (base) => {
    await withBmps("odd", [odd], async ([test]) => {
      const message = (await assertRejects(
        () => runCompare({ base, test: [test], max_changed: 1500, dilate: DEFAULT_DILATE }),
        CompareError,
        "frames of different sizes are never compared",
      )).message
      assert(message.includes(`${W}x${H}`), message)
      assert(message.includes(`${W + 1}x${H}`), message)
    })
  })
})

Deno.test("a file that is not a decodable frame is refused with its name", async () => {
  const dir = await Deno.makeTempDir({ prefix: "bnplay-compare-" })
  try {
    const junk = `${dir}/junk.bmp`
    await Deno.writeTextFile(junk, "not an image")
    await withBmps("base", BASE, async (base) => {
      await assertRejects(
        () => runCompare({ base, test: [junk], max_changed: 1500, dilate: DEFAULT_DILATE }),
        CompareError,
        junk,
      )
    })
  } finally {
    await Deno.remove(dir, { recursive: true })
  }
})
