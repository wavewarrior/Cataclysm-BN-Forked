/// Process and git helpers shared by the gate and the driver.
import { dirname, join } from "@std/path"

export type RunOptions = {
  cwd?: string
  env?: Record<string, string>
  stdin?: string
}

export type RunResult = { code: number; stdout: string; stderr: string }

/// Run to completion with captured output. Never throws on a non-zero exit.
export async function run(cmd: string[], opts: RunOptions = {}): Promise<RunResult> {
  const child = new Deno.Command(cmd[0], {
    args: cmd.slice(1),
    cwd: opts.cwd,
    env: opts.env,
    stdin: opts.stdin === undefined ? "null" : "piped",
    stdout: "piped",
    stderr: "piped",
  }).spawn()
  if (opts.stdin !== undefined) {
    const w = child.stdin.getWriter()
    await w.write(new TextEncoder().encode(opts.stdin))
    await w.close()
  }
  const out = await child.output()
  const dec = new TextDecoder()
  return { code: out.code, stdout: dec.decode(out.stdout), stderr: dec.decode(out.stderr) }
}

/// Run to completion appending stdout+stderr to `logPath`. Returns the exit code.
export async function runToLog(
  cmd: string[],
  logPath: string,
  opts: RunOptions = {},
): Promise<number> {
  await Deno.mkdir(dirname(logPath), { recursive: true })
  const log = await Deno.open(logPath, { write: true, create: true, append: true })
  try {
    await log.write(new TextEncoder().encode(`$ ${cmd.join(" ")}\n`))
    const child = new Deno.Command(cmd[0], {
      args: cmd.slice(1),
      cwd: opts.cwd,
      env: opts.env,
      stdin: "null",
      stdout: "piped",
      stderr: "piped",
    }).spawn()
    const pump = async (stream: ReadableStream<Uint8Array>) => {
      for await (const chunk of stream) await log.write(chunk)
    }
    await Promise.all([pump(child.stdout), pump(child.stderr)])
    const status = await child.status
    await log.write(new TextEncoder().encode(`\n[exit ${status.code}]\n`))
    return status.code
  } finally {
    log.close()
  }
}

/// Like runToLog, but kills the child as soon as `stopWhen` accepts a (colour-stripped) output line.
/// A long suite that has already failed should not run to the end. Returns the exit code and
/// whether it was cut short.
export async function runToLogUntil(
  cmd: string[],
  logPath: string,
  stopWhen: (line: string) => boolean,
  opts: RunOptions = {},
): Promise<{ code: number; stopped: boolean }> {
  await Deno.mkdir(dirname(logPath), { recursive: true })
  const log = await Deno.open(logPath, { write: true, create: true, append: true })
  let stopped = false
  try {
    await log.write(new TextEncoder().encode(`$ ${cmd.join(" ")}\n`))
    const child = new Deno.Command(cmd[0], {
      args: cmd.slice(1),
      cwd: opts.cwd,
      env: opts.env,
      stdin: "null",
      stdout: "piped",
      stderr: "piped",
    }).spawn()
    const pump = async (stream: ReadableStream<Uint8Array>) => {
      let pending = ""
      const decoder = new TextDecoder()
      for await (const chunk of stream) {
        await log.write(chunk)
        pending += decoder.decode(chunk, { stream: true })
        const lines = pending.split(/\r?\n/)
        pending = lines.pop() ?? ""
        // deno-lint-ignore no-control-regex -- stripping ANSI colour escapes
        if (!stopped && lines.some((l) => stopWhen(l.replace(/\x1b\[[0-9;]*[A-Za-z]/g, "")))) {
          stopped = true
          // /T: the test binary is a grandchild of `deno task`.
          if (Deno.build.os === "windows") {
            await run(["taskkill", "/PID", String(child.pid), "/T", "/F"])
          } else child.kill("SIGKILL")
        }
      }
    }
    await Promise.all([pump(child.stdout), pump(child.stderr)])
    const status = await child.status
    await log.write(
      new TextEncoder().encode(`\n[exit ${status.code}${stopped ? ", stopped early" : ""}]\n`),
    )
    return { code: status.code, stopped }
  } finally {
    log.close()
  }
}

/// `git <args>` in `cwd`; throws with git's stderr on failure. Returns trimmed stdout.
export async function git(cwd: string, ...args: string[]): Promise<string> {
  const r = await run(["git", ...args], { cwd })
  if (r.code !== 0) throw new Error(`git ${args.join(" ")} failed (${r.code}): ${r.stderr.trim()}`)
  return r.stdout.trim()
}

/// Absolute per-worktree git dir. Stamps, verdicts and logs live in `<gitDir>/factory/`.
export function gitDir(cwd: string): Promise<string> {
  return git(cwd, "rev-parse", "--absolute-git-dir")
}

/// The primary checkout that owns the shared `.git` (herdr only creates worktrees from it).
export async function mainRepoRoot(cwd: string): Promise<string> {
  return dirname(await git(cwd, "rev-parse", "--path-format=absolute", "--git-common-dir"))
}

export async function factoryDir(cwd: string): Promise<string> {
  const dir = join(await gitDir(cwd), "factory")
  await Deno.mkdir(dir, { recursive: true })
  return dir
}

/// Last `n` lines of a file, or a note when it does not exist.
export async function tailFile(path: string, n: number): Promise<string> {
  try {
    const lines = (await Deno.readTextFile(path)).split(/\r?\n/)
    return lines.slice(-n).join("\n")
  } catch {
    return `(no log at ${path})`
  }
}

/// Format seconds as `12.3s`.
export function fmtSeconds(ms: number): string {
  return `${(ms / 1000).toFixed(1)}s`
}
