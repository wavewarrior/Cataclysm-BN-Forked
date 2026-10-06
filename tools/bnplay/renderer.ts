/**
 * Renderer oracles: what a windowed Trial says about the frames its Episode captured. They run
 * supervisor-side over the capture results and the frame files, and report numbers, never images.
 *
 * The lessons of the Windows A/B harness are built in. A change is never judged against a constant:
 * it is judged against a paired null, two captures of the same state, and the threshold scales off
 * that noise. A toggled state must be restored (1 -> 0 -> 1) and the restored frame must match the
 * original again, or the toggle is not what changed the picture. Readiness comes from the game's
 * own messages, never from the pixels settling. And frames of different sizes are never compared.
 *
 * Also here: the `window_size` check every windowed Episode gets, the Trial's fixed window against
 * the size each capture reports.
 */
import type { DriverResponse } from "./client.ts"
import { decodeFrame, type Frame, frameDelta } from "./frames.ts"
import type { OracleResult, Progress } from "./oracles.ts"
import type { RendererOracleSpec, Trial } from "./trial.ts"

/** What a successful `capture` reports (see capture_contract.ts). */
type Reported = {
  mode: string
  frame: string
  width: number
  height: number
  window_width: number
  window_height: number
}

/** One capture the agent tagged, and when it happened relative to the game's messages. */
type Taken = {
  /** The request id in the transcript. */
  index: number
  tag: string
  turn?: number
  reported: Reported
  /** Observations seen up to and including this one. */
  seq: number
}

function isReported(value: unknown): value is Reported {
  if (typeof value !== "object" || value === null) return false
  const c = value as Record<string, unknown>
  return typeof c.frame === "string" && typeof c.width === "number" &&
    typeof c.height === "number" && typeof c.window_width === "number" &&
    typeof c.window_height === "number"
}

const size = (w: number, h: number) => `${w}x${h}`

/** A delta as the report writes it: enough digits to tell 0.0004 from 0.004. */
const num = (n: number) => String(Number(n.toPrecision(3)))

export class RendererRun {
  readonly #trial: Pick<Trial, "rendererOracles" | "window">
  /** Every successful capture, tagged or not, for the window-size check. */
  readonly #captures: { index: number; turn?: number; reported: Reported }[] = []
  readonly #taken: Taken[] = []
  /** Every message the game logged, with the observation it arrived in. */
  readonly #messages: { seq: number; text: string }[] = []
  readonly #frames = new Map<string, Promise<Frame>>()
  #seq = 0
  #lastIndex = 0

  constructor(trial: Pick<Trial, "rendererOracles" | "window">) {
    this.#trial = trial
  }

  /** Called with each answered request, in order; `tag` is what the agent called a capture. */
  observe(
    { index, tag, response }: { index: number; tag?: string; response: DriverResponse },
  ): void {
    this.#lastIndex = index
    if (response.status !== "ok") return
    this.#seq++
    if (Array.isArray(response.new_messages)) {
      for (const text of response.new_messages) {
        if (typeof text === "string") this.#messages.push({ seq: this.#seq, text })
      }
    }
    if (!isReported(response.capture)) return
    const turn = typeof response.turn === "number" ? response.turn : undefined
    this.#captures.push({ index, turn, reported: response.capture })
    if (tag !== undefined) {
      this.#taken.push({ index, tag, turn, reported: response.capture, seq: this.#seq })
    }
  }

  /** The verdicts of the window-size check and of every capture oracle, in report order. */
  async results(progress: Progress): Promise<OracleResult[]> {
    const results: OracleResult[] = []
    if (this.#trial.window) results.push(this.#windowSize())
    for (const spec of this.#trial.rendererOracles) results.push(await this.#judge(spec, progress))
    return results
  }

  /**
   * The Trial fixed the window; every capture reports the window it came from and the frame size.
   * The window must be the one asked for. The frame is the window times a whole scale, the same on
   * both axes: 1 in logical pixels, 2 on a Retina display's device pixels.
   */
  #windowSize(): OracleResult {
    const window = this.#trial.window!
    const name = "window_size"
    if (this.#captures.length === 0) {
      return {
        name,
        result: "skipped",
        note: "no capture was taken, so the window size was not measured",
        decisive: false,
      }
    }
    const wanted = size(window.width, window.height)
    for (const { index, turn, reported: c } of this.#captures) {
      const at = { index, ...(turn === undefined ? {} : { turn }) }
      if (c.window_width !== window.width || c.window_height !== window.height) {
        return {
          name,
          result: "fail",
          first_fail: {
            ...at,
            why: `the Trial fixed a ${wanted} window but the game reports a ` +
              `${size(c.window_width, c.window_height)} one`,
          },
          decisive: true,
        }
      }
      const scale = c.width / window.width
      if (!Number.isInteger(scale) || scale < 1 || c.height !== window.height * scale) {
        return {
          name,
          result: "fail",
          first_fail: {
            ...at,
            why: `the ${c.mode} frame is ${size(c.width, c.height)}, not the ${wanted} window ` +
              `times a whole scale (1 in logical pixels, 2 on a Retina display)`,
          },
          decisive: true,
        }
      }
    }
    return { name, result: "pass", decisive: false }
  }

  #load(capture: Taken): Promise<Frame> {
    const path = capture.reported.frame
    let loading = this.#frames.get(path)
    if (!loading) {
      loading = Deno.readFile(path).then(decodeFrame)
      this.#frames.set(path, loading)
    }
    return loading
  }

  async #judge(spec: RendererOracleSpec, progress: Progress): Promise<OracleResult> {
    const failing = spec.severity === "fail" ? "fail" : "warn"
    const fail = (at: { index: number; turn?: number }, why: string): OracleResult => ({
      name: spec.name,
      result: failing,
      first_fail: { index: at.index, ...(at.turn === undefined ? {} : { turn: at.turn }), why },
      decisive: spec.severity === "fail",
    })

    // The captures the oracle needs, and what is still missing from the Episode.
    const withTag = (tag: string) => this.#taken.filter((c) => c.tag === tag)
    const [reference, nullPartner] = withTag(spec.original)
    const toggled = spec.toggled === undefined ? undefined : withTag(spec.toggled)[0]
    const restored = spec.restored === undefined ? undefined : withTag(spec.restored)[0]
    const missing: string[] = []
    if (!reference || !nullPartner) {
      const have = reference ? 1 : 0
      missing.push(
        `two captures tagged "${spec.original}" (the original and its paired null), ` +
          `the Episode made ${have}`,
      )
    }
    if (spec.toggled !== undefined && !toggled) missing.push(`a capture tagged "${spec.toggled}"`)
    if (spec.restored !== undefined && !restored) {
      missing.push(`a capture tagged "${spec.restored}"`)
    }
    if (missing.length > 0) {
      const why = `${spec.kind} needs ${missing.join(" and ")}`
      if (progress === "complete") return fail({ index: this.#lastIndex }, why)
      return { name: spec.name, result: "inconclusive", note: why, decisive: false }
    }
    const captures = [reference!, nullPartner!, toggled, restored].filter((c) => c !== undefined)

    if (spec.ready !== undefined) {
      const unready = this.#unready(spec.ready, captures)
      if (unready) {
        return fail(
          unready,
          `captured "${unready.tag}" before the game logged "${spec.ready}" since the capture ` +
            `before it: readiness is read from the game's messages, not from the pixels`,
        )
      }
    }

    const frames: Frame[] = []
    for (const capture of captures) {
      try {
        const frame = await this.#load(capture)
        const { width, height } = capture.reported
        if (frame.width !== width || frame.height !== height) {
          return fail(
            capture,
            `the frame file is ${size(frame.width, frame.height)} but the capture reported ` +
              `${size(width, height)}`,
          )
        }
        frames.push(frame)
      } catch (e) {
        return fail(
          capture,
          `cannot read the frame ${capture.reported.frame}: ${(e as Error).message}`,
        )
      }
    }
    const odd = frames.findIndex((f) =>
      f.width !== frames[0].width || f.height !== frames[0].height
    )
    if (odd > 0) {
      const first = captures[0]
      return fail(
        captures[odd],
        `frames of different sizes are never compared: "${captures[odd].tag}" is ` +
          `${size(frames[odd].width, frames[odd].height)} (${captures[odd].reported.mode}), ` +
          `"${first.tag}" is ${
            size(frames[0].width, frames[0].height)
          } (${first.reported.mode}); ` +
          `capture every state in the same mode`,
      )
    }

    const delta = (a: Frame, b: Frame) => frameDelta(a, b, spec.region)
    const noise = delta(frames[0], frames[1])
    if (spec.kind === "paired_null") {
      if (noise > spec.maxNoise) {
        return fail(
          nullPartner!,
          `two captures of "${spec.original}" differ by ${num(noise)}, more than max_noise ` +
            `${num(spec.maxNoise)}: the scene is not steady enough to judge a change against`,
        )
      }
      return pass(spec, `noise ${num(noise)}`)
    }

    // The effect of the toggle must clear the noise of the null by `factor`.
    const threshold = spec.factor * noise
    const effect = delta(frames[0], frames[2])
    if (!(effect > threshold) || effect === 0) {
      return fail(
        toggled!,
        `"${spec.toggled}" differs from "${spec.original}" by ${num(effect)}, not more than ` +
          `${num(spec.factor)}x the paired null's noise of ${num(noise)} (${num(threshold)}): ` +
          `the toggle did nothing the noise does not explain`,
      )
    }
    if (spec.kind === "diff_vs_null") {
      return pass(spec, `noise ${num(noise)}, effect ${num(effect)}`)
    }

    // 1 -> 0 -> 1: the restored state is the original again, and not merely the toggled one.
    const restoredDelta = delta(frames[0], frames[3])
    if (delta(frames[2], frames[3]) === 0) {
      return fail(
        restored!,
        `"${spec.restored}" is identical to "${spec.toggled}": the restore did not take, so ` +
          `neither pair measures the toggle`,
      )
    }
    if (restoredDelta > threshold) {
      return fail(
        restored!,
        `"${spec.restored}" differs from "${spec.original}" by ${num(restoredDelta)}, more than ` +
          `${num(spec.factor)}x the paired null's noise of ${num(noise)} (${num(threshold)}): ` +
          `the state did not restore, or the scene drifted`,
      )
    }
    return pass(spec, `noise ${num(noise)}, effect ${num(effect)}, restored ${num(restoredDelta)}`)
  }

  /**
   * The first capture of a state other than the one captured before it that no `ready` message
   * preceded: after the previous capture of the oracle's frames in the Episode, or since boot.
   */
  #unready(ready: string, captures: Taken[]): Taken | undefined {
    for (const capture of captures) {
      const at = this.#taken.indexOf(capture)
      const before = this.#taken[at - 1]
      if (before?.tag === capture.tag) continue
      const since = before?.seq ?? 0
      const seen = this.#messages.some((m) =>
        m.seq > since && m.seq <= capture.seq && m.text.includes(ready)
      )
      if (!seen) return capture
    }
    return undefined
  }
}

function pass(spec: RendererOracleSpec, note: string): OracleResult {
  return { name: spec.name, result: "pass", note, decisive: spec.severity === "fail" }
}
