/**
 * Runs cata_test-tiles with a live progress bar: done/total, pass/fail counts, elapsed, ETA and
 * the test that just finished. Failures are printed above the bar as they happen, the full Catch2
 * output goes to a log file, and the slowest tests are listed at the end.
 *
 * Usage: deno task test:progress [--exe <path>] [--log <path>] [catch2 args...]
 *   e.g. deno task test:progress --rng-seed 1 "[rot]"
 *
 * Without --exe the newest existing cata_test-tiles binary is used, and its path and mtime are
 * printed, so a stale binary is visible before the run starts.
 * @module
 */

const exeName = Deno.build.os === "windows" ? "cata_test-tiles.exe" : "cata_test-tiles"

const candidates = (): string[] => {
  const found = [exeName]
  for (const dir of ["out/msvc/tests", "out/build"]) {
    try {
      for (const entry of Deno.readDirSync(dir)) {
        if (!entry.isDirectory) continue
        found.push(`${dir}/${entry.name}/${exeName}`, `${dir}/${entry.name}/tests/${exeName}`)
      }
    } catch { /* directory absent */ }
  }
  return found
}

const newestExe = (): string | undefined =>
  candidates()
    .flatMap((path) => {
      try {
        return [{ path, mtime: Deno.statSync(path).mtime?.getTime() ?? 0 }]
      } catch {
        return []
      }
    })
    .sort((a, b) => b.mtime - a.mtime)[0]?.path

const takeOption = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name)
  if (i < 0) return undefined
  const [, value] = args.splice(i, 2)
  return value
}

const clock = (ms: number): string => {
  const s = Math.round(ms / 1000)
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`
}

// Escapes only on a real terminal, so piped output and captured logs stay plain text.
const color = Deno.stdout.isTerminal() && !Deno.env.get("NO_COLOR")
const paint = (code: number) => (s: string) => color ? `\x1b[${code}m${s}\x1b[0m` : s
const red = paint(31)
const green = paint(32)
const dim = paint(2)

const args = [...Deno.args]
const exe = takeOption(args, "--exe") ?? newestExe()
const logPath = takeOption(args, "--log") ?? "out/test-progress.log"
if (!exe) {
  console.error(`test:progress: no ${exeName} found; pass --exe`)
  Deno.exit(2)
}

const env = { SDL_ASSERT: Deno.env.get("SDL_ASSERT") ?? "always_ignore" }
console.log(`${exe}  (built ${Deno.statSync(exe).mtime?.toLocaleString()})`)

const listNames = async (listArgs: string[]): Promise<Set<string>> => {
  const { stdout } = await new Deno.Command(exe, {
    args: [...listArgs, "--list-tests", "--verbosity", "quiet"],
    env,
    stdin: "null",
    stderr: "null",
  }).output()
  return new Set(new TextDecoder().decode(stdout).split(/\r?\n/).filter((l) => l.trim()))
}
const testNames = await listNames(args)
// Cases Catch2 counts as "failed as expected" rather than failed.
const expectedToFail = await listNames(["[!shouldfail],[!mayfail]"])
const total = testNames.size
console.log(`${total} test cases; full output -> ${logPath}`)

await Deno.mkdir(logPath.replace(/[\\/][^\\/]*$/, ""), { recursive: true }).catch(() => {})
const log = await Deno.open(logPath, { write: true, create: true, truncate: true })

const child = new Deno.Command(exe, {
  args: [...args, "--durations", "yes"],
  env,
  stdin: "null",
  stdout: "piped",
  stderr: "piped",
}).spawn()

const start = Date.now()
// Catch2 reruns a case once per leaf SECTION and prints the case's name and duration after each
// run, so a name repeats: count each case once and add up its runs.
const durations = new Map<string, number>()
const failed = new Set<string>()
const failedAsExpected = new Set<string>()
let catchTotals = "" // Catch2's own "test cases: ..." line, the ground truth for the counts
// Cases run one at a time and Catch2 prints a case's failures before its own duration line, so
// a failure seen since the previous case finished belongs to the next listed case to finish.
let pendingFail = false
let last = ""
const tty = Deno.stdout.isTerminal()
const encoder = new TextEncoder()
const write = (s: string) => Deno.stdout.writeSync(encoder.encode(s))

const bar = (final = false) => {
  const done = durations.size
  const width = 30
  const filled = total ? Math.min(width, Math.round((done / total) * width)) : 0
  const elapsed = Date.now() - start
  const eta = done ? clock((elapsed / done) * Math.max(0, total - done)) : "?"
  const pct = total ? ((done / total) * 100).toFixed(1) : "?"
  const passed = done - failed.size - failedAsExpected.size
  const expected = failedAsExpected.size ? ` xfail ${failedAsExpected.size}` : ""
  const counts = `${green(`pass ${passed}`)} ${
    failed.size ? red(`fail ${failed.size}`) : "fail 0"
  }${expected}`
  const head = `[${"#".repeat(filled)}${"-".repeat(width - filled)}] ${done}/${total} ${pct}% | `
  const tail = ` | ${clock(elapsed)} ETA ${eta} | `
  const plainCounts = `pass ${passed} fail ${failed.size}${expected}`
  // A bar wider than the console wraps, and "\r" then only redraws its last row.
  const room = tty
    ? Deno.consoleSize().columns - 1 - head.length - plainCounts.length - tail.length
    : 60
  const line = `${head}${counts}${tail}${dim(last.slice(0, Math.max(0, room)))}`
  if (tty) write(`\r\x1b[2K${line}`)
  else if (final || done % 25 === 0) console.log(line)
}

const onLine = (line: string) => {
  // --durations also reports every SECTION; only listed test cases advance the bar.
  const duration = line.match(/^(\d+\.\d+) s: (.+)$/)
  if (duration && testNames.has(duration[2])) {
    const [, seconds, name] = duration
    durations.set(name, (durations.get(name) ?? 0) + Number(seconds))
    last = name
    if (pendingFail) {
      pendingFail = false
      if (expectedToFail.has(name)) failedAsExpected.add(name)
      else if (!failed.has(name)) {
        failed.add(name)
        if (tty) write("\r\x1b[2K")
        console.log(`${red("FAIL")} ${name}`)
      }
    }
    bar()
    return
  }
  if (/FAILED:|failed with exception|Fatal error condition/.test(line)) pendingFail = true
  if (line.startsWith("test cases:")) catchTotals = line
}

const pump = async (stream: ReadableStream<Uint8Array>, isStdout: boolean) => {
  let pending = ""
  for await (const chunk of stream.pipeThrough(new TextDecoderStream())) {
    await log.write(encoder.encode(chunk))
    pending += chunk
    const lines = pending.split(/\r?\n/)
    pending = lines.pop() ?? ""
    // Catch2 reports on stdout; stderr carries only game logging.
    if (isStdout) lines.forEach(onLine)
  }
}

const interval = tty ? setInterval(bar, 1000) : undefined
bar()
await Promise.all([pump(child.stdout, true), pump(child.stderr, false)])
const { code } = await child.status
clearInterval(interval)
log.close()

if (tty) write("\r\x1b[2K")
bar(true)
console.log("")
console.log("slowest:")
for (const [name, seconds] of [...durations].toSorted((a, b) => b[1] - a[1]).slice(0, 5)) {
  console.log(`  ${seconds.toFixed(1).padStart(7)} s  ${name}`)
}
console.log(
  failed.size
    ? red(`${failed.size} failed:\n  ${[...failed].join("\n  ")}`)
    : green(`no unexpected failures in ${durations.size} cases`),
)
if (failedAsExpected.size) {
  console.log(
    `${failedAsExpected.size} failed as expected:\n  ${[...failedAsExpected].join("\n  ")}`,
  )
}
if (catchTotals) console.log(`Catch2: ${catchTotals.replace(/\s+/g, " ")}`)
// A crash or abort prints no duration line for the case it killed.
if (code !== 0 && durations.size < total) {
  console.log(
    red(`stopped early: ${total - durations.size} cases never finished; last finished: ${last}`),
  )
}
console.log(`exit ${code}; log ${logPath}`)
// A crash exit (e.g. 0xC0000005, negative here) does not survive Deno.exit's byte: keep it non-zero.
Deno.exit(code === 0 ? 0 : code > 0 && code < 256 ? code : 1)
