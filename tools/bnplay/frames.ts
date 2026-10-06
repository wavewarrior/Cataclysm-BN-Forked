/**
 * Frames: decoding the two image formats `capture` writes (the final composite as a 24 or 32 bit
 * BMP, the state view as an 8 bit PNG) into plain RGB, and the one number the renderer oracles
 * compare frames by. Images never leave this module: a report carries the numbers, not pixels.
 *
 * The decoders cover exactly what the game's writers produce (the swapchain dump's bottom-up BMP,
 * SDL_image's non-interlaced PNG) and refuse anything else by name instead of guessing, so no image
 * library is a dependency of the tool.
 */

/** A decoded frame: `rgb` holds `width * height` pixels as R, G, B bytes, top row first. */
export type Frame = { width: number; height: number; rgb: Uint8Array }

/** A part of a frame as fractions of its size: `[x, y, width, height]`, each in 0..1. */
export type Region = [number, number, number, number]

/** The file is not an image this module decodes. */
export class FrameError extends Error {
  constructor(message: string) {
    super(message)
    this.name = "FrameError"
  }
}

const PNG_SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]

/** Decodes a BMP or a PNG, whichever the bytes say it is. */
export async function decodeFrame(bytes: Uint8Array): Promise<Frame> {
  if (bytes.length >= 2 && bytes[0] === 0x42 && bytes[1] === 0x4d) return decodeBmp(bytes)
  if (PNG_SIGNATURE.every((b, i) => bytes[i] === b)) return await decodePng(bytes)
  throw new FrameError("the file is neither a BMP nor a PNG")
}

function viewOf(bytes: Uint8Array): DataView {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
}

/** Uncompressed BMP, 24 or 32 bits per pixel, bottom-up (a positive height) or top-down. */
export function decodeBmp(bytes: Uint8Array): Frame {
  if (bytes.length < 54) throw new FrameError("the BMP is shorter than its header")
  const view = viewOf(bytes)
  const offset = view.getUint32(10, true)
  const headerSize = view.getUint32(14, true)
  if (headerSize < 40) throw new FrameError(`unsupported BMP header of ${headerSize} bytes`)
  const width = view.getInt32(18, true)
  const signedHeight = view.getInt32(22, true)
  const bits = view.getUint16(28, true)
  const compression = view.getUint32(30, true)
  if (compression !== 0) throw new FrameError(`unsupported BMP compression ${compression}`)
  if (bits !== 24 && bits !== 32) throw new FrameError(`unsupported BMP depth of ${bits} bits`)
  const height = Math.abs(signedHeight)
  if (width <= 0 || height === 0) throw new FrameError("the BMP has no pixels")
  const bytesPerPixel = bits / 8
  const stride = (width * bytesPerPixel + 3) & ~3
  if (bytes.length < offset + stride * height) throw new FrameError("the BMP is truncated")
  const rgb = new Uint8Array(width * height * 3)
  for (let y = 0; y < height; y++) {
    const row = offset + stride * (signedHeight > 0 ? height - 1 - y : y)
    let out = y * width * 3
    for (let x = 0, at = row; x < width; x++, at += bytesPerPixel) {
      rgb[out++] = bytes[at + 2]
      rgb[out++] = bytes[at + 1]
      rgb[out++] = bytes[at]
    }
  }
  return { width, height, rgb }
}

async function inflate(compressed: Uint8Array): Promise<Uint8Array> {
  const stream = new Blob([compressed as BlobPart]).stream()
    .pipeThrough(new DecompressionStream("deflate"))
  return new Uint8Array(await new Response(stream).arrayBuffer())
}

/** Samples per pixel of each PNG colour type. */
const PNG_CHANNELS: Record<number, number> = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }

/** Non-interlaced PNG, 8 bits per channel: grey, RGB, palette, grey+alpha and RGBA. */
export async function decodePng(bytes: Uint8Array): Promise<Frame> {
  const view = viewOf(bytes)
  let header: { width: number; height: number; colourType: number } | undefined
  let palette: Uint8Array = new Uint8Array(0)
  const data: Uint8Array[] = []
  for (let at = 8; at + 8 <= bytes.length;) {
    const length = view.getUint32(at)
    const kind = String.fromCharCode(...bytes.subarray(at + 4, at + 8))
    const body = bytes.subarray(at + 8, at + 8 + length)
    if (body.length < length) throw new FrameError("the PNG is truncated")
    if (kind === "IHDR") {
      const depth = body[8]
      if (depth !== 8) throw new FrameError(`unsupported PNG depth of ${depth} bits`)
      if (body[12] !== 0) throw new FrameError("unsupported interlaced PNG")
      header = {
        width: viewOf(body).getUint32(0),
        height: viewOf(body).getUint32(4),
        colourType: body[9],
      }
    } else if (kind === "PLTE") palette = body
    else if (kind === "IDAT") data.push(body)
    else if (kind === "IEND") break
    at += 12 + length
  }
  if (!header) throw new FrameError("the PNG has no header")
  const { width, height, colourType } = header
  const channels = PNG_CHANNELS[colourType]
  if (channels === undefined) throw new FrameError(`unsupported PNG colour type ${colourType}`)
  if (width === 0 || height === 0) throw new FrameError("the PNG has no pixels")
  if (data.length === 0) throw new FrameError("the PNG has no image data")

  const joined = new Uint8Array(data.reduce((n, part) => n + part.length, 0))
  let filled = 0
  for (const part of data) {
    joined.set(part, filled)
    filled += part.length
  }
  const raw = await inflate(joined).catch(() => {
    throw new FrameError("the PNG's image data does not decompress")
  })
  const stride = width * channels
  if (raw.length < (stride + 1) * height) throw new FrameError("the PNG's image data is truncated")

  // Undo the per-row filters in place, each row against the one above it.
  const pixels = new Uint8Array(stride * height)
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)]
    const source = y * (stride + 1) + 1
    const row = y * stride
    for (let i = 0; i < stride; i++) {
      const left = i >= channels ? pixels[row + i - channels] : 0
      const up = y > 0 ? pixels[row - stride + i] : 0
      const upLeft = y > 0 && i >= channels ? pixels[row - stride + i - channels] : 0
      let predicted: number
      switch (filter) {
        case 0:
          predicted = 0
          break
        case 1:
          predicted = left
          break
        case 2:
          predicted = up
          break
        case 3:
          predicted = (left + up) >> 1
          break
        case 4: {
          const p = left + up - upLeft
          const pa = Math.abs(p - left)
          const pb = Math.abs(p - up)
          const pc = Math.abs(p - upLeft)
          predicted = pa <= pb && pa <= pc ? left : pb <= pc ? up : upLeft
          break
        }
        default:
          throw new FrameError(`unsupported PNG row filter ${filter}`)
      }
      pixels[row + i] = (raw[source + i] + predicted) & 0xff
    }
  }

  const rgb = new Uint8Array(width * height * 3)
  for (let p = 0; p < width * height; p++) {
    const at = p * channels
    const out = p * 3
    if (colourType === 2 || colourType === 6) {
      rgb[out] = pixels[at]
      rgb[out + 1] = pixels[at + 1]
      rgb[out + 2] = pixels[at + 2]
    } else if (colourType === 3) {
      const entry = pixels[at] * 3
      if (entry + 2 >= palette.length) throw new FrameError("the PNG indexes past its palette")
      rgb[out] = palette[entry]
      rgb[out + 1] = palette[entry + 1]
      rgb[out + 2] = palette[entry + 2]
    } else {
      rgb[out] = rgb[out + 1] = rgb[out + 2] = pixels[at]
    }
  }
  return { width, height, rgb }
}

/** The pixel rectangle `region` covers in a frame of this size (the whole frame when absent). */
function pixelsOf(frame: Frame, region: Region | undefined) {
  const [x, y, w, h] = region ?? [0, 0, 1, 1]
  const left = Math.min(frame.width - 1, Math.floor(x * frame.width))
  const top = Math.min(frame.height - 1, Math.floor(y * frame.height))
  const right = Math.max(left + 1, Math.min(frame.width, Math.ceil((x + w) * frame.width)))
  const bottom = Math.max(top + 1, Math.min(frame.height, Math.ceil((y + h) * frame.height)))
  return { left, top, right, bottom }
}

/**
 * How much two frames of the same size differ: the mean absolute difference of their colour
 * channels over `region`, as a fraction of the full range (0 identical, 1 black against white).
 * Frames of different sizes are never compared; the caller says so before asking.
 */
export function frameDelta(a: Frame, b: Frame, region?: Region): number {
  if (a.width !== b.width || a.height !== b.height) {
    throw new FrameError(
      `cannot compare a ${a.width}x${a.height} frame with a ${b.width}x${b.height} one`,
    )
  }
  const { left, top, right, bottom } = pixelsOf(a, region)
  let total = 0
  for (let y = top; y < bottom; y++) {
    const from = (y * a.width + left) * 3
    const to = (y * a.width + right) * 3
    for (let i = from; i < to; i++) total += Math.abs(a.rgb[i] - b.rgb[i])
  }
  return total / (255 * 3 * (right - left) * (bottom - top))
}
