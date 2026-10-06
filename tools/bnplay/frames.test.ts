/**
 * The frame decoders, against images written the way the game's writers write them: a bottom-up
 * BMP from the swapchain dump, and PNGs from SDL_image, which filters each row adaptively and may
 * keep an alpha channel. A frame decodes to the same pixels whatever the file format said.
 */
import { assertAlmostEquals, assertEquals, assertRejects, assertThrows } from "@std/assert"
import { decodeBmp, decodeFrame, frameDelta, FrameError } from "./frames.ts"

const WIDTH = 7
const HEIGHT = 5

/** A frame with every pixel distinct enough that a wrong filter or row order shows. */
function sample(): Uint8Array {
  const rgb = new Uint8Array(WIDTH * HEIGHT * 3)
  for (let i = 0; i < rgb.length; i++) rgb[i] = (i * 37 + (i >> 3) * 11) & 0xff
  return rgb
}

function u32be(n: number): number[] {
  return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255]
}

async function deflate(bytes: Uint8Array): Promise<Uint8Array> {
  const stream = new Blob([bytes as BlobPart]).stream().pipeThrough(
    new CompressionStream("deflate"),
  )
  return new Uint8Array(await new Response(stream).arrayBuffer())
}

function crc32(bytes: Uint8Array): number {
  let crc = ~0
  for (const b of bytes) {
    crc ^= b
    for (let k = 0; k < 8; k++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1))
  }
  return ~crc >>> 0
}

function chunk(kind: string, data: Uint8Array): number[] {
  const body = new Uint8Array([...new TextEncoder().encode(kind), ...data])
  return [...u32be(data.length), ...body, ...u32be(crc32(body))]
}

const paeth = (a: number, b: number, c: number) => {
  const p = a + b - c
  const pa = Math.abs(p - a)
  const pb = Math.abs(p - b)
  const pc = Math.abs(p - c)
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c
}

/** A PNG of `pixels` (`channels` bytes each) with every row filtered by `filterOf(row)`. */
async function png(
  pixels: Uint8Array,
  channels: number,
  colourType: number,
  filterOf: (row: number) => number,
  extra: number[] = [],
): Promise<Uint8Array> {
  const stride = WIDTH * channels
  const rows: number[] = []
  for (let y = 0; y < HEIGHT; y++) {
    const filter = filterOf(y)
    rows.push(filter)
    for (let i = 0; i < stride; i++) {
      const raw = pixels[y * stride + i]
      const left = i >= channels ? pixels[y * stride + i - channels] : 0
      const up = y > 0 ? pixels[(y - 1) * stride + i] : 0
      const upLeft = y > 0 && i >= channels ? pixels[(y - 1) * stride + i - channels] : 0
      const predicted = [0, left, up, (left + up) >> 1, paeth(left, up, upLeft)][filter]
      rows.push((raw - predicted) & 255)
    }
  }
  const header = new Uint8Array([...u32be(WIDTH), ...u32be(HEIGHT), 8, colourType, 0, 0, 0])
  return new Uint8Array([
    0x89,
    0x50,
    0x4e,
    0x47,
    0x0d,
    0x0a,
    0x1a,
    0x0a,
    ...chunk("IHDR", header),
    ...extra,
    ...chunk("IDAT", await deflate(new Uint8Array(rows))),
    ...chunk("IEND", new Uint8Array(0)),
  ])
}

Deno.test("a PNG decodes to its pixels whichever row filter wrote it", async () => {
  const rgb = sample()
  for (const filter of [0, 1, 2, 3, 4]) {
    const frame = await decodeFrame(await png(rgb, 3, 2, () => filter))
    assertEquals([frame.width, frame.height], [WIDTH, HEIGHT])
    assertEquals(frame.rgb, rgb, `filter ${filter}`)
  }
  // SDL_image picks a filter per row.
  const mixed = await decodeFrame(await png(rgb, 3, 2, (row) => row % 5))
  assertEquals(mixed.rgb, rgb)
})

Deno.test("an RGBA, grey or palette PNG decodes to RGB", async () => {
  const rgb = sample()
  const rgba = new Uint8Array(WIDTH * HEIGHT * 4)
  const grey = new Uint8Array(WIDTH * HEIGHT)
  for (let p = 0; p < WIDTH * HEIGHT; p++) {
    rgba.set(rgb.subarray(p * 3, p * 3 + 3), p * 4)
    rgba[p * 4 + 3] = 255
    grey[p] = rgb[p * 3]
  }
  assertEquals((await decodeFrame(await png(rgba, 4, 6, (row) => row % 5))).rgb, rgb)

  const greyFrame = await decodeFrame(await png(grey, 1, 0, () => 1))
  for (let p = 0; p < WIDTH * HEIGHT; p++) {
    assertEquals([...greyFrame.rgb.subarray(p * 3, p * 3 + 3)], [grey[p], grey[p], grey[p]])
  }

  const indexes = new Uint8Array(WIDTH * HEIGHT).map((_, i) => i % 3)
  const palette = chunk("PLTE", new Uint8Array([255, 0, 0, 0, 255, 0, 0, 0, 255]))
  const indexed = await decodeFrame(await png(indexes, 1, 3, () => 2, palette))
  assertEquals([...indexed.rgb.subarray(0, 9)], [255, 0, 0, 0, 255, 0, 0, 0, 255])
})

/** A BMP of `rgb`: 24 bits padded to 4 bytes, or 32 bits; bottom-up unless `topDown`. */
function bmp(rgb: Uint8Array, bits: 24 | 32, topDown = false): Uint8Array {
  const bytesPerPixel = bits / 8
  const stride = (WIDTH * bytesPerPixel + 3) & ~3
  const pixels = new Uint8Array(stride * HEIGHT)
  for (let y = 0; y < HEIGHT; y++) {
    const row = (topDown ? y : HEIGHT - 1 - y) * stride
    for (let x = 0; x < WIDTH; x++) {
      const from = (y * WIDTH + x) * 3
      pixels[row + x * bytesPerPixel] = rgb[from + 2]
      pixels[row + x * bytesPerPixel + 1] = rgb[from + 1]
      pixels[row + x * bytesPerPixel + 2] = rgb[from]
    }
  }
  const header = new DataView(new ArrayBuffer(54))
  header.setUint8(0, 0x42)
  header.setUint8(1, 0x4d)
  header.setUint32(2, 54 + pixels.length, true)
  header.setUint32(10, 54, true)
  header.setUint32(14, 40, true)
  header.setInt32(18, WIDTH, true)
  header.setInt32(22, topDown ? -HEIGHT : HEIGHT, true)
  header.setUint16(26, 1, true)
  header.setUint16(28, bits, true)
  header.setUint32(34, pixels.length, true)
  return new Uint8Array([...new Uint8Array(header.buffer), ...pixels])
}

Deno.test("a BMP decodes to its pixels, 24 or 32 bits, bottom-up or top-down", () => {
  const rgb = sample()
  for (const [bits, topDown] of [[24, false], [32, false], [24, true], [32, true]] as const) {
    const frame = decodeBmp(bmp(rgb, bits, topDown))
    assertEquals([frame.width, frame.height], [WIDTH, HEIGHT])
    assertEquals(frame.rgb, rgb, `${bits} bits, top-down ${topDown}`)
  }
})

Deno.test("what is not a decodable image is refused by name", async () => {
  await assertRejects(() => decodeFrame(new Uint8Array([1, 2, 3, 4])), FrameError, "neither")
  const truncated = bmp(sample(), 24).subarray(0, 80)
  assertThrows(() => decodeBmp(truncated), FrameError, "truncated")
  const compressed = bmp(sample(), 24)
  compressed[30] = 1
  assertThrows(() => decodeBmp(compressed), FrameError, "compression")
  const sixteen = bmp(sample(), 24)
  sixteen[28] = 16
  assertThrows(() => decodeBmp(sixteen), FrameError, "depth")
  const whole = await png(sample(), 3, 2, () => 0)
  await assertRejects(() => decodeFrame(whole.subarray(0, 40)), FrameError)
})

Deno.test("the delta of two frames is the mean channel difference as a share of the range", async () => {
  const a = await decodeFrame(bmp(sample(), 24))
  assertEquals(frameDelta(a, a), 0)

  const black = { width: 4, height: 2, rgb: new Uint8Array(4 * 2 * 3) }
  const white = { width: 4, height: 2, rgb: new Uint8Array(4 * 2 * 3).fill(255) }
  assertEquals(frameDelta(black, white), 1)

  // One pixel of eight, fully flipped.
  const one = { ...black, rgb: new Uint8Array(black.rgb) }
  one.rgb.fill(255, 0, 3)
  assertAlmostEquals(frameDelta(black, one), 1 / 8)
  // The region is a share of the frame: the left half holds that pixel, the right half none.
  assertAlmostEquals(frameDelta(black, one, [0, 0, 0.5, 1]), 1 / 4)
  assertEquals(frameDelta(black, one, [0.5, 0, 0.5, 1]), 0)
  // The top row is half the frame and holds the pixel.
  assertAlmostEquals(frameDelta(black, one, [0, 0, 1, 0.5]), 1 / 4)

  assertThrows(
    () => frameDelta(black, { width: 2, height: 2, rgb: new Uint8Array(12) }),
    FrameError,
    "cannot compare",
  )
})
