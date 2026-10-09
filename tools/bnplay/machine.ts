/**
 * What the machine has to spare right now: load, memory and swap, read from macOS (`sysctl`,
 * `vm_stat`) or, on Windows, from `Win32_OperatingSystem` (Windows has no load average, and
 * `Deno.systemMemoryInfo()` reports 0 available and 0 swap free there). Every reading that cannot
 * be taken is left out and named in `unreadable`, never guessed.
 */
import { IS_WINDOWS } from "./config.ts"

export type MachineSample = {
  /** 1, 5 and 15 minute load averages. */
  load_average?: [number, number, number]
  cpus: number
  swap_total_mb?: number
  swap_used_mb?: number
  swap_free_mb?: number
  /** Free, inactive and speculative pages: what a new game can take without paging others out. */
  memory_available_mb?: number
  /** Readings that could not be taken, with the reason. */
  unreadable?: string[]
}

async function output(command: string, args: string[]): Promise<string> {
  const out = await new Deno.Command(command, { args, stdout: "piped", stderr: "piped" }).output()
  if (!out.success) {
    throw new Error(`${command} ${args.join(" ")} failed: ${new TextDecoder().decode(out.stderr)}`)
  }
  return new TextDecoder().decode(out.stdout)
}

const round = (n: number) => Math.round(n * 10) / 10

/** A running process as `Win32_Process` reports it; `age` is in seconds. */
export type WindowsProcess = { pid: number; ppid: number; age: number; command: string }

/**
 * Every process on this Windows machine except the PowerShell that lists them, from CIM. A command
 * line this user may not read is empty.
 */
export async function windowsProcesses(): Promise<WindowsProcess[]> {
  const script = "@(Get-CimInstance Win32_Process | Where-Object ProcessId -ne $PID | " +
    "ForEach-Object { [pscustomobject]@{ pid = [int]$_.ProcessId; " +
    "ppid = [int]$_.ParentProcessId; age = $(if ($_.CreationDate) " +
    "{ [int]((Get-Date) - $_.CreationDate).TotalSeconds } else { 0 }); " +
    "command = [string]$_.CommandLine } })"
  const text = (await output("powershell", [
    "-NoProfile",
    "-Command",
    `ConvertTo-Json -Compress -InputObject ${script}`,
  ])).trim()
  return text ? JSON.parse(text) : []
}

/** `vm.loadavg`: `{ 2.06 1.52 1.40 }`. */
export function parseLoadAverage(text: string): [number, number, number] {
  const m = /^\{\s*([\d.]+)\s+([\d.]+)\s+([\d.]+)\s*\}/.exec(text.trim())
  if (!m) throw new Error(`unrecognised vm.loadavg: ${text.trim()}`)
  return [round(Number(m[1])), round(Number(m[2])), round(Number(m[3]))]
}

/** `vm.swapusage`: `total = 12288.00M  used = 10865.19M  free = 1422.81M  (encrypted)`. */
export function parseSwapUsage(text: string): { total: number; used: number; free: number } {
  const field = (name: string) => {
    const m = new RegExp(`${name} = ([\\d.]+)M`).exec(text)
    if (!m) throw new Error(`no ${name} in vm.swapusage: ${text.trim()}`)
    return round(Number(m[1]))
  }
  return { total: field("total"), used: field("used"), free: field("free") }
}

/** `vm_stat`: a page size header and `Pages <kind>: <count>.` lines. */
export function parseAvailableMemory(text: string): number {
  const size = /page size of (\d+) bytes/.exec(text)
  if (!size) throw new Error("no page size in vm_stat output")
  let pages = 0
  for (const kind of ["free", "inactive", "speculative"]) {
    const m = new RegExp(`^Pages ${kind}:\\s+(\\d+)\\.`, "m").exec(text)
    if (!m) throw new Error(`no "Pages ${kind}" line in vm_stat output`)
    pages += Number(m[1])
  }
  return round((pages * Number(size[1])) / (1024 * 1024))
}

export async function sampleMachine(): Promise<MachineSample> {
  const sample: MachineSample = { cpus: navigator.hardwareConcurrency }
  const unreadable: string[] = []
  if (IS_WINDOWS) {
    // Free physical memory, and the commit limit as "swap": Windows pages against commit, so free
    // commit is what a new game can take before the machine starts to thrash. CIM reports KiB.
    try {
      const [free, commitTotal, commitFree] = (await output("powershell", [
        "-NoProfile",
        "-Command",
        "$o = Get-CimInstance Win32_OperatingSystem; " +
        '"$($o.FreePhysicalMemory) $($o.TotalVirtualMemorySize) $($o.FreeVirtualMemory)"',
      ])).trim().split(/\s+/).map((kib) => round(Number(kib) / 1024))
      sample.memory_available_mb = free
      sample.swap_total_mb = commitTotal
      sample.swap_free_mb = commitFree
      sample.swap_used_mb = round(commitTotal - commitFree)
    } catch (e) {
      unreadable.push(`memory and commit: ${(e as Error).message}`)
    }
    sample.unreadable = [...unreadable, "load: Windows has no load average"]
    return sample
  }
  try {
    sample.load_average = parseLoadAverage(await output("sysctl", ["-n", "vm.loadavg"]))
  } catch (e) {
    unreadable.push(`load: ${(e as Error).message}`)
  }
  try {
    const swap = parseSwapUsage(await output("sysctl", ["-n", "vm.swapusage"]))
    sample.swap_total_mb = swap.total
    sample.swap_used_mb = swap.used
    sample.swap_free_mb = swap.free
  } catch (e) {
    unreadable.push(`swap: ${(e as Error).message}`)
  }
  try {
    sample.memory_available_mb = parseAvailableMemory(await output("vm_stat", []))
  } catch (e) {
    unreadable.push(`memory: ${(e as Error).message}`)
  }
  if (unreadable.length > 0) sample.unreadable = unreadable
  return sample
}
