/** Shared helpers for tools/devloop-bench.ts (wayfinder #137). */

type Rec = Record<string, unknown>

export async function sh(cmd: string[], cwd?: string): Promise<{ out: string; code: number }> {
  const p = new Deno.Command(cmd[0], { args: cmd.slice(1), cwd, stdout: "piped", stderr: "piped" })
  const r = await p.output()
  return {
    out: new TextDecoder().decode(r.stdout) + new TextDecoder().decode(r.stderr),
    code: r.code,
  }
}

export function cores(): number {
  return parseInt(
    new TextDecoder().decode(
      new Deno.Command("sysctl", { args: ["-n", "hw.ncpu"], stdout: "piped" }).outputSync().stdout,
    ).trim(),
    10,
  )
}

export function uptimeLoad(): number {
  const u = new TextDecoder().decode(
    new Deno.Command("sysctl", { args: ["-n", "vm.loadavg"], stdout: "piped" }).outputSync().stdout,
  )
  return parseFloat(u.replace(/[{}]/g, "").trim().split(/\s+/)[0])
}

function vmPageouts(): number {
  const s = new TextDecoder().decode(
    new Deno.Command("vm_stat", { args: [], stdout: "piped" }).outputSync().stdout,
  )
  const m = s.match(/Pageouts:\s+(\d+)/)
  return m ? parseInt(m[1], 10) : NaN
}

/** Real build/game processes only: `ninja` matched the NinjaRMMAgent monitor. */
export function foreignBuilders(): string[] {
  const r = new Deno.Command("pgrep", {
    args: ["-fl", "(^|/)ninja( |$)|cataclysm-bn|cata_test|ld\\.prd"],
    stdout: "piped",
  }).outputSync()
  return new TextDecoder().decode(r.stdout)
    .split("\n")
    .filter((l) => l.trim() && !l.includes("devloop"))
    .map((l) => l.slice(0, 100))
}

export async function envRecord(): Promise<Rec> {
  const a = vmPageouts()
  await new Promise((res) => setTimeout(res, 10_000))
  const b = vmPageouts()
  const swap = new TextDecoder().decode(
    new Deno.Command("sysctl", { args: ["-n", "vm.swapusage"], stdout: "piped" }).outputSync()
      .stdout,
  ).trim()
  return {
    load1: uptimeLoad(),
    cores: cores(),
    pageouts_per_s: Math.round(((b - a) / 10) * 10) / 10,
    swap,
    foreign: foreignBuilders(),
  }
}

/**
 * Ticket #137 hygiene: start only when load1 < 4, pageouts/s < 5, and no
 * foreign builders. Our own build raising load during the run is expected;
 * the gate only judges the START. Records and retries every 120 s.
 */
export async function idleGate(): Promise<void> {
  for (;;) {
    const e = await envRecord()
    const ok = (e.load1 as number) < 4 &&
      (e.pageouts_per_s as number) < 5 &&
      ((e.foreign as string[]) ?? []).length === 0
    if (ok) {
      console.error(`idle gate passed: ${JSON.stringify(e)}`)
      return
    }
    console.error(`idle gate waiting: ${JSON.stringify(e)} (retry in 120s)`)
    await new Promise((res) => setTimeout(res, 120_000))
  }
}
