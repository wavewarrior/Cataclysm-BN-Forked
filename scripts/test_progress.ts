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

const red = (s: string) => `\x1b[31m${s}\x1b[0m`
const green = (s: string) => `\x1b[32m${s}\x1b[0m`
const dim = (s: string) => `\x1b[2m${s}\x1b[0m`

const args = [...Deno.args]
const exe = takeOption(args, "--exe") ?? newestExe()
const logPath = takeOption(args, "--log") ?? "out/test-progress.log"
if (!exe) {
  console.error(`test:progress: no ${exeName} found; pass --exe`)
  Deno.exit(2)
}

const env = { SDL_ASSERT: Deno.env.get("SDL_ASSERT") ?? "always_ignore" }
console.log(`${exe}  (built ${Deno.statSync(exe).mtime?.toLocaleString()})`)

const listing = await new Deno.Command(exe, {
  args: [...args, "--list-tests", "--verbosity", "quiet"],
  env,
  stdin: "null",
  stderr: "null",
}).output()
const testNames = new Set(
  new TextDecoder().decode(listing.stdout).split(/\r?\n/).filter((l) => l.trim()),
)
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
const durations: { name: string; seconds: number }[] = []
const failed: string[] = []
// Cases run one at a time and Catch2 prints a case's failures before its own duration line, so
// a failure seen since the previous case finished belongs to the next listed case to finish.
let pendingFail = false
let last = ""
const tty = Deno.stdout.isTerminal()
const encoder = new TextEncoder()
const write = (s: string) => Deno.stdout.writeSync(encoder.encode(s))

const bar = (final = false) => {
  const done = durations.length
  const width = 30
  const filled = total ? Math.min(width, Math.round((done / total) * width)) : 0
  const elapsed = Date.now() - start
  const eta = done ? clock((elapsed / done) * Math.max(0, total - done)) : "?"
  const pct = total ? ((done / total) * 100).toFixed(1) : "?"
  const counts = `${green(`pass ${done - failed.length}`)} ${
    failed.length ? red(`fail ${failed.length}`) : "fail 0"
  }`
  const head = `[${"#".repeat(filled)}${"-".repeat(width - filled)}] ${done}/${total} ${pct}% | `
  const tail = ` | ${clock(elapsed)} ETA ${eta} | `
  const plainCounts = `pass ${done - failed.length} fail ${failed.length}`
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
    durations.push({ name, seconds: Number(seconds) })
    last = name
    if (pendingFail) {
      pendingFail = false
      failed.push(name)
      if (tty) write("\r\x1b[2K")
      console.log(`${red("FAIL")} ${name}`)
    }
    bar()
    return
  }
  if (/FAILED:|failed with exception|Fatal error condition/.test(line)) pendingFail = true
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
for (const { name, seconds } of durations.toSorted((a, b) => b.seconds - a.seconds).slice(0, 5)) {
  console.log(`  ${seconds.toFixed(1).padStart(7)} s  ${name}`)
}
console.log(
  failed.length
    ? red(`${failed.length} failed:\n  ${failed.join("\n  ")}`)
    : green(`all ${durations.length} passed`),
)
// A crash or abort prints no duration line for the case it killed.
if (code !== 0 && durations.length < total) {
  console.log(
    red(`stopped early: ${total - durations.length} cases never finished; last finished: ${last}`),
  )
}
console.log(`exit ${code}; log ${logPath}`)
Deno.exit(code)
