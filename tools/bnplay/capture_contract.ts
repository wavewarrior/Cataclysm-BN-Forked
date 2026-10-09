/**
 * Capture contract: `capture` writes the frame the game last drew and the map snapshot of the same
 * turn, and says how big the frame is. Shared by every driver implementation and, like the other
 * contracts, it asserts external behaviour only: what the response says, what the files hold, and
 * that nothing is left behind.
 *
 *   capture dir=<absolute directory> [mode=final|state]
 *     final (the default)  the final composite the window shows, lighting included, as a BMP
 *     state                an offscreen re-render of the drawn state, with no lighting tonemap and
 *                          no interface, as a PNG: a state view, not a lighting oracle
 *   answers `capture`: mode, label, frame, map, width, height, window_width, window_height
 *   files: `turn-<turn>-<n>-final.bmp` or `-state.png`, and `turn-<turn>-<n>-map.json`; `n`
 *                          counts the captures, so several of one turn each keep their own
 *   refused (`outcome: refused`, reason `no_drawable`): the window is hidden or minimised, or (a
 *                          final composite only) occluded, as with the screen locked, so there is
 *                          nothing to draw into. No frame is written and an earlier one is never
 *                          returned in its place.
 * A windowless game has no window to capture: `capture` is a protocol error there. A capture takes
 * no game time.
 */
import { assert, assertEquals, assertNotEquals } from "@std/assert"
import { delay } from "@std/async"
import { normalize, SEPARATOR } from "@std/path"
import type { Driver, DriverResponse } from "./client.ts"
import { type ContractTarget, groupAlive } from "./contract.ts"

/** What a successful capture reports. */
type Captured = {
  mode: string
  label: string
  frame: string
  map: string
  width: number
  height: number
  window_width: number
  window_height: number
}

/** Files a capture could leave in the shared places the old file-triggered dump used. */
const GLOBAL_DUMPS = /^cata_(frame_\d+\.bmp|map_\d+\.json)$/

async function globalDumps(): Promise<string[]> {
  const found: string[] = []
  for await (const entry of Deno.readDir("/tmp")) {
    if (GLOBAL_DUMPS.test(entry.name)) found.push(entry.name)
  }
  return found.sort()
}

function captured(res: DriverResponse): Captured {
  assertEquals(res.status, "ok", JSON.stringify(res))
  assertEquals(res.outcome, "completed", JSON.stringify(res))
  const c = res.capture as Captured | undefined
  assert(c !== undefined && typeof c === "object", `no capture member: ${JSON.stringify(res)}`)
  return c
}

/** Width and height a BMP's header claims, or undefined when the file is not a BMP. */
function bmpSize(bytes: Uint8Array): { width: number; height: number } | undefined {
  if (bytes.length < 26 || bytes[0] !== 0x42 || bytes[1] !== 0x4d) return undefined
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  return { width: view.getUint32(18, true), height: Math.abs(view.getInt32(22, true)) }
}

/** Width and height a PNG's IHDR claims, or undefined when the file is not a PNG. */
function pngSize(bytes: Uint8Array): { width: number; height: number } | undefined {
  const signature = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]
  if (bytes.length < 24 || signature.some((b, i) => bytes[i] !== b)) return undefined
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  return { width: view.getUint32(16), height: view.getUint32(20) }
}

/** Registers the capture contract under `name`; what it asks of the driver follows `target.window`. */
export function runCaptureContract(name: string, target: ContractTarget): void {
  const session = (
    title: string,
    fn: (driver: Driver, dir: string, t: Deno.TestContext) => Promise<void>,
  ) =>
    Deno.test({
      name: `driver capture contract: ${name}: ${title}`,
      sanitizeOps: false,
      sanitizeResources: false,
      async fn(t) {
        const dir = await Deno.makeTempDir({ prefix: "bnplay-capture-" })
        const dumpsBefore = await globalDumps()
        let driver: Driver | undefined
        try {
          driver = await target.spawn()
          assertEquals((await driver.send({ cmd: "ping" }, target.bootTimeoutMs)).status, "ok")
          await fn(driver, dir, t)
          assertEquals(driver.noise, [], "the protocol channel carried non-protocol lines")
          // Whatever the test did, the driver must still serve.
          assertEquals((await driver.send({ cmd: "ping" })).status, "ok")
        } finally {
          await driver?.close()
          await Deno.remove(dir, { recursive: true })
        }
        if (driver) assert(!(await groupAlive(driver.pgid)), "process group left running")
        assertEquals(
          await globalDumps(),
          dumpsBefore,
          "a capture wrote to a shared global filename",
        )
      },
    })

  const turnOf = async (driver: Driver) => (await driver.send({ cmd: "state" })).turn as number
  const namesIn = (dir: string) => [...Deno.readDirSync(dir)].map((e) => e.name).sort()

  const { window } = target
  if (!window) {
    session(
      "a windowless game refuses capture as a protocol error and takes no time",
      async (driver, dir) => {
        const turn = await turnOf(driver)
        for (const request of [{ cmd: "capture", dir }, { cmd: "capture", dir, mode: "state" }]) {
          const res = await driver.send(request)
          assertEquals(res.status, "error", JSON.stringify(res))
          assert(String(res.error).includes("windowed"), `the error names the mode: ${res.error}`)
        }
        assertEquals(await turnOf(driver), turn)
        assertEquals(namesIn(dir), [], "a refused capture left files")
      },
    )
    return
  }

  session(
    "a capture returns the frame, the paired map and the frame size",
    async (driver, dir, t) => {
      await t.step("the final composite and the map of the same turn", async () => {
        const turn = await turnOf(driver)
        const res = await driver.send({ cmd: "capture", dir })
        const c = captured(res)
        assertEquals(res.turn, turn)
        assertEquals(res.time_passed, false)
        assertEquals(c.mode, "final")
        assertEquals(c.label, "final composite")
        // Both files are named by the turn, under the directory asked for.
        for (const path of [c.frame, c.map]) {
          assertEquals(normalize(path).startsWith(dir + SEPARATOR), true, path)
          assert(path.includes(`turn-${turn}-`), `named by turn ${turn}: ${path}`)
        }
        assertNotEquals(c.frame, c.map)
        // The frame is a non-empty image whose own header agrees with the reported size.
        const frame = await Deno.readFile(c.frame)
        assert(frame.length > 1000, `the frame is ${frame.length} bytes`)
        assertEquals(bmpSize(frame), { width: c.width, height: c.height })
        // The size is the window's, as many device pixels per logical pixel on a HiDPI display.
        assertEquals([c.window_width, c.window_height], [window.width, window.height])
        assert(c.width % window.width === 0 && c.width > 0, `width ${c.width}`)
        assertEquals(c.height / window.height, c.width / window.width)
        // The map snapshot is the same turn's.
        const map = JSON.parse(await Deno.readTextFile(c.map))
        assertEquals(map.turn, turn)
        assert(Array.isArray(map.player) && map.player.length === 3, JSON.stringify(map.player))
      })

      await t.step(
        "mode state is a state view: offscreen, labelled, paired with the map",
        async () => {
          const turn = await turnOf(driver)
          const res = await driver.send({ cmd: "capture", dir, mode: "state" })
          const c = captured(res)
          assertEquals(res.turn, turn)
          assertEquals(res.time_passed, false)
          assertEquals(c.mode, "state")
          assertEquals(c.label, "state view")
          const frame = await Deno.readFile(c.frame)
          assertEquals(pngSize(frame), { width: c.width, height: c.height })
          assertEquals([c.window_width, c.window_height], [window.width, window.height])
          // The state view is drawn in logical pixels: the window's own size.
          assertEquals([c.width, c.height], [window.width, window.height])
          assertEquals(JSON.parse(await Deno.readTextFile(c.map)).turn, turn)
          // It does not replace the final composite of the same turn.
          const final = captured(await driver.send({ cmd: "capture", dir }))
          assertNotEquals(final.frame, c.frame)
          assert((await Deno.stat(c.frame)).size > 0 && (await Deno.stat(final.frame)).size > 0)
        },
      )

      await t.step("a capture takes no game time and a later turn gets its own files", async () => {
        const first = captured(await driver.send({ cmd: "capture", dir }))
        const turn = await turnOf(driver)
        assertEquals(await turnOf(driver), turn)
        const waited = await driver.send({ cmd: "wait", turns: 2 })
        assertEquals(waited.status, "ok", JSON.stringify(waited))
        const second = captured(await driver.send({ cmd: "capture", dir }))
        assertEquals((await driver.send({ cmd: "state" })).turn, waited.turn)
        assertNotEquals(second.frame, first.frame)
        assertNotEquals(second.map, first.map)
        assert(second.frame.includes(`turn-${waited.turn}-`), second.frame)
        // The earlier turn's files are still what they were.
        assert((await Deno.stat(first.frame)).size > 0)
        assertEquals(JSON.parse(await Deno.readTextFile(first.map)).turn, turn)
      })

      await t.step("several captures of one turn each keep their own files", async () => {
        const turn = await turnOf(driver)
        const made = []
        for (const mode of ["final", "state", "final", "final"]) {
          made.push(captured(await driver.send({ cmd: "capture", dir, mode })))
        }
        assertEquals(await turnOf(driver), turn)
        assertEquals(new Set(made.flatMap((c) => [c.frame, c.map])).size, made.length * 2)
        for (const c of made) {
          assert(c.frame.includes(`turn-${turn}-`), c.frame)
          assert((await Deno.stat(c.frame)).size > 0, c.frame)
          assertEquals(JSON.parse(await Deno.readTextFile(c.map)).turn, turn)
        }
      })

      await t.step(
        "a bad request is a protocol error, leaves no file and costs no time",
        async () => {
          const turn = await turnOf(driver)
          const before = namesIn(dir)
          for (
            const request of [
              { cmd: "capture" },
              { cmd: "capture", dir: "" },
              { cmd: "capture", dir: 7 },
              { cmd: "capture", dir: "relative/dir" },
              { cmd: "capture", dir, mode: "lighting" },
              { cmd: "capture", dir, mode: 3 },
            ]
          ) {
            const res = await driver.send(request)
            assertEquals(res.status, "error", JSON.stringify(request))
            assert(String(res.error).length > 0)
          }
          assertEquals(namesIn(dir), before)
          assertEquals(await turnOf(driver), turn)
        },
      )
    },
  )

  if (target.hideWindow) {
    const hideWindow = target.hideWindow
    session(
      "a window with no drawable is refused, never answered with an old frame",
      async (driver, dir, t) => {
        const turn = await turnOf(driver)
        // A frame from before the window went away: it must not come back.
        const old = captured(await driver.send({ cmd: "capture", dir }))
        const files = namesIn(dir)

        const restore = await hideWindow(driver)
        let restored = false
        try {
          for (const mode of ["final", "state"]) {
            await t.step(
              `${mode}: outcome refused, reason no_drawable, no frame, no time`,
              async () => {
                const res = await driver.send({ cmd: "capture", dir, mode })
                assertEquals(res.status, "ok", JSON.stringify(res))
                assertEquals(res.outcome, "refused", JSON.stringify(res))
                assertEquals(res.reason, "no_drawable")
                assertEquals(res.time_passed, false)
                assertEquals(res.turn, turn)
                assertEquals("capture" in res, false, "a refusal carries no capture")
                assertEquals(namesIn(dir), files)
              },
            )
          }
          await restore()
          restored = true
        } finally {
          if (!restored) await restore().catch(() => undefined)
        }

        await t.step("once the window is back a capture is fresh again", async () => {
          // A window takes a moment to come back from the dock.
          let res = await driver.send({ cmd: "capture", dir })
          for (let attempt = 0; res.outcome === "refused" && attempt < 20; attempt++) {
            await delay(500)
            res = await driver.send({ cmd: "capture", dir })
          }
          const c = captured(res)
          assertNotEquals(c.frame, old.frame, "a new capture never takes the name of an old one")
          assert((await Deno.stat(old.frame)).size > 0, "the old capture is left alone")
          assertEquals(bmpSize(await Deno.readFile(c.frame)), { width: c.width, height: c.height })
        })
      },
    )
  }
}
