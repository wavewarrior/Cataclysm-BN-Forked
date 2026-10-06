/**
 * Which lines of the game's debug.log were logged inside a window of wall-clock time. The log
 * stamps each line `HH:MM:SS.mmm` with no date, and lines without a stamp continue the line above.
 */
import { assertEquals } from "@std/assert"
import { linesInWindow } from "./gamelog.ts"

const at = (h: number, m: number, s: number, ms = 0, day = 1) =>
  new Date(2026, 0, day, h, m, s, ms).getTime()

const LOG = [
  "",
  "",
  "-----------------------------------------",
  "12:00:00.100 : Starting log.",
  "12:00:03.000 ERROR : before.cpp:1 [load] logged before the window",
  "12:00:05.500 ERROR : first.cpp:2 [tick] first inside",
  "    a continuation line of the first",
  "",
  "12:00:07.250 WARNING : second.cpp:3 [tick] second inside",
  "12:00:09.900 ERROR : after.cpp:4 [bye] logged after the window",
  "    a continuation line of the one after",
  "12:00:10.100 : Log shutdown.",
  "-----------------------------------------",
  "",
].join("\n")

Deno.test("only lines stamped inside the window are returned, with their continuation lines", () => {
  assertEquals(linesInWindow(LOG, { fromMs: at(12, 0, 4), toMs: at(12, 0, 9) }), [
    "12:00:05.500 ERROR : first.cpp:2 [tick] first inside",
    "    a continuation line of the first",
    "12:00:07.250 WARNING : second.cpp:3 [tick] second inside",
  ])
})

Deno.test("an empty window, or one that closes before it opens, returns nothing", () => {
  assertEquals(linesInWindow(LOG, { fromMs: at(12, 0, 4), toMs: at(12, 0, 5) }), [])
  assertEquals(linesInWindow(LOG, { fromMs: at(12, 0, 9), toMs: at(12, 0, 4) }), [])
  assertEquals(linesInWindow("", { fromMs: at(12, 0, 4), toMs: at(12, 0, 9) }), [])
})

Deno.test("a line stamped within the clock's rounding of a boundary counts as outside it", () => {
  const log = [
    "12:00:05.000 ERROR : the last boot line",
    "12:00:05.002 ERROR : stamped just after readiness, as the clock rounds",
    "12:00:05.200 ERROR : well after readiness",
    "12:00:06.998 ERROR : stamped just before quit, as the clock rounds",
    "12:00:07.000 ERROR : the first shutdown line",
  ].join("\n")
  assertEquals(linesInWindow(log, { fromMs: at(12, 0, 5, 0), toMs: at(12, 0, 7, 0) }), [
    "12:00:05.200 ERROR : well after readiness",
  ])
})

Deno.test("a window that spans midnight still picks the right lines", () => {
  const log = [
    "23:59:50.000 ERROR : booting, the day before",
    "23:59:58.900 ERROR : boot line just before readiness",
    "23:59:59.500 ERROR : idle before midnight",
    "00:00:01.000 ERROR : idle after midnight",
    "    and its continuation",
    "00:00:02.500 ERROR : shutdown after quit",
  ].join("\n")
  assertEquals(linesInWindow(log, { fromMs: at(23, 59, 59, 0, 1), toMs: at(0, 0, 2, 0, 2) }), [
    "23:59:59.500 ERROR : idle before midnight",
    "00:00:01.000 ERROR : idle after midnight",
    "    and its continuation",
  ])
})

Deno.test("a millisecond field the game rounds up to 1000 reads as the next second", () => {
  const log = ["12:00:05.1000 ERROR : stamped as 12:00:06.000", "12:00:09.100 ERROR : late"].join(
    "\n",
  )
  assertEquals(linesInWindow(log, { fromMs: at(12, 0, 5, 500), toMs: at(12, 0, 7, 0) }), [
    "12:00:05.1000 ERROR : stamped as 12:00:06.000",
  ])
})
