/// Process helpers for `check`: capture, tee to a log, and a tail.
import { join } from "@std/path"

export type Run = { code: number; stdout: string; stderr: string }

/// Extra environment for a child. `undefined` removes a variable the parent had, which is how the
/// Linux test run drops the window-system variables a forked child must not share.
export type Env = Record<string, string | undefined>

const decode = (bytes: Uint8Array) => new TextDecoder().decode(bytes)

function childEnv(env?: Env): Record<string, string> | undefined {
  if (env === undefined) return undefined
  const merged: Record<string, string> = {}
  for (const [key, value] of Object.entries({ ...Deno.env.toObject(), ...env })) {
    if (value !== undefined) merged[key] = value
  }
  return merged
}

/// Run to completion with captured output. Never throws on a non-zero exit; throws when the
/// program itself cannot start, which is a broken environment rather than a failing step.
export async function run(cmd: string[], opts: { cwd?: string; env?: Env } = {}): Promise<Run> {
  const result = await new Deno.Command(cmd[0], {
    args: cmd.slice(1),
    cwd: opts.cwd,
    env: childEnv(opts.env),
    stdout: "piped",
    stderr: "piped",
  }).output()
  return { code: result.code, stdout: decode(result.stdout), stderr: decode(result.stderr) }
}

/// Run a long step (a build, the suite) with its output teed live to our own stdout and stderr, so
/// a human attached to the lane watches it, and appended live to `logPath`, so it survives the
/// session. The caller still gets the whole stdout, where the summary lines are.
export async function runTee(
  cmd: string[],
  logPath: string,
  opts: { cwd?: string; env?: Env } = {},
): Promise<Run> {
  await Deno.mkdir(join(logPath, ".."), { recursive: true })
  const log = await Deno.open(logPath, { create: true, append: true, write: true })
  const child = new Deno.Command(cmd[0], {
    args: cmd.slice(1),
    cwd: opts.cwd,
    env: childEnv(opts.env),
    stdout: "piped",
    stderr: "piped",
  }).spawn()
  const echo = async (
    readable: ReadableStream<Uint8Array>,
    live: (chunk: Uint8Array) => Promise<unknown>,
  ): Promise<string> => {
    let text = ""
    for await (const chunk of readable) {
      text += decode(chunk)
      await live(chunk)
      await log.write(chunk)
    }
    return text
  }
  let code = -1
  let stdout = ""
  let stderr = ""
  try {
    ;[stdout, stderr, code] = await Promise.all([
      echo(child.stdout, (c) => Deno.stdout.write(c)),
      echo(child.stderr, (c) => Deno.stderr.write(c)),
      child.status.then((s) => s.code),
    ])
  } finally {
    await log.write(new TextEncoder().encode(`\n$ ${cmd.join(" ")}\n(exit ${code})\n`))
    log.close()
  }
  return { code, stdout, stderr }
}

/// Last `n` non-empty lines of a file, or a note when it is unreadable.
export async function tail(path: string, n: number): Promise<string> {
  try {
    const lines = (await Deno.readTextFile(path)).split("\n").filter((l) => l.trim() !== "")
    return lines.slice(Math.max(0, lines.length - n)).join("\n")
  } catch {
    return `(no log at ${path})`
  }
}
