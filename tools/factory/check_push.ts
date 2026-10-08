/// pre-push verdict for a factory lane (called by .githooks/pre-push when FACTORY_LANE=1).
/// A push is allowed only to `refs/heads/factory/*`, and only for the exact commit that passed the
/// full gate and got `VERDICT: PASS` from the reviewer. Reads git's pre-push lines on stdin.
import { join } from "@std/path"
import { config } from "./config.ts"
import type { Stamp } from "./gate.ts"
import { gitDir } from "./util.ts"

export type PushRef = { localRef: string; localSha: string; remoteRef: string; remoteSha: string }

export type PushEvidence = {
  stamp: Stamp | undefined
  /// First line of `<gitdir>/factory/review-<sha>.md` per pushed sha; undefined when absent.
  reviewFirstLine: (sha: string) => string | undefined
}

const ZERO = /^0+$/

export function parsePushLines(text: string): PushRef[] {
  return text.split(/\r?\n/).filter((l) => l.trim() !== "").map((line) => {
    const [localRef, localSha, remoteRef, remoteSha] = line.trim().split(/\s+/)
    return { localRef, localSha, remoteRef, remoteSha }
  })
}

/// Reasons the push must be refused; empty means allowed.
export function checkPush(refs: PushRef[], evidence: PushEvidence): string[] {
  const problems: string[] = []
  if (refs.length === 0) return ["nothing to push"]
  for (const ref of refs) {
    if (!ref.remoteRef.startsWith(`refs/heads/${config.branchPrefix}`)) {
      problems.push(`${ref.remoteRef}: a lane may only push refs/heads/${config.branchPrefix}*`)
      continue
    }
    if (ZERO.test(ref.localSha)) {
      problems.push(`${ref.remoteRef}: deleting a remote branch is not allowed`)
      continue
    }
    const { stamp } = evidence
    if (!stamp) problems.push("no gate stamp: run `deno task gate --tier full`")
    else if (!stamp.ok) {
      problems.push(`gate stamp is not ok (failed: ${stamp.failedSteps.join(", ")})`)
    } else if (stamp.tier !== "full") problems.push(`gate stamp tier is ${stamp.tier}, need full`)
    else if (stamp.headSha !== ref.localSha) {
      problems.push(`stamp sha mismatch: stamp ${stamp.headSha}, pushing ${ref.localSha}`)
    }
    const verdict = evidence.reviewFirstLine(ref.localSha)
    if (verdict === undefined) problems.push(`no review verdict for ${ref.localSha}`)
    else if (verdict.trim() !== "VERDICT: PASS") {
      problems.push(`review verdict is "${verdict.trim()}"`)
    }
  }
  return problems
}

async function readEvidence(): Promise<PushEvidence> {
  const dir = join(await gitDir(Deno.cwd()), "factory")
  const stamp = await Deno.readTextFile(join(dir, "gate-stamp.json")).then(
    (t) => JSON.parse(t) as Stamp,
    () => undefined,
  )
  return {
    stamp,
    reviewFirstLine: (sha) => {
      try {
        return Deno.readTextFileSync(join(dir, `review-${sha}.md`)).split(/\r?\n/, 1)[0]
      } catch {
        return undefined
      }
    },
  }
}

if (import.meta.main) {
  const input = new TextDecoder().decode(await new Response(Deno.stdin.readable).arrayBuffer())
  const problems = checkPush(parsePushLines(input), await readEvidence())
  if (problems.length > 0) {
    console.error("pre-push (factory lane): push refused")
    for (const p of problems) console.error(`  - ${p}`)
    Deno.exit(1)
  }
}
