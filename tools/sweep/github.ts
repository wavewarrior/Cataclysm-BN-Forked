/// The `gh` CLI behind the `Gh` interface, pointed at one repo. Bodies travel through a temp file
/// (`--body-file`), because a markdown body with backticks and newlines does not survive being an
/// argv element. Nothing in the tests reaches this file: they inject a fake `Gh`.
import { run } from "../check/exec.ts"
import type { Gh, IssueRef } from "./types.ts"

const LABEL_COLOR = "5319e7"
const LABEL_DESCRIPTION = "Found by a platform sweep (tools/sweep)"

type GhContext = { repo: string; labels: Set<string> }

async function orThrow(args: string[], what: string): Promise<string> {
  const result = await run(args)
  if (result.code !== 0) {
    throw new Error(`${what} failed (exit ${result.code}): ${result.stderr.trim().slice(0, 300)}`)
  }
  return result.stdout
}

/// The body file for one `--body-file` call, removed by the caller's `using`.
async function bodyFile(body: string): Promise<string> {
  const path = await Deno.makeTempFile({ prefix: "sweep-body-", suffix: ".md" })
  await Deno.writeTextFile(path, body)
  return path
}

/// Labels are created on first use, not assumed: a fresh fork has no `platform:*` labels and
/// `gh issue create` refuses an unknown label. `--force` keeps this idempotent.
async function ensureLabel(context: GhContext, label: string): Promise<void> {
  if (context.labels.has(label)) return
  await orThrow(
    [
      "gh",
      "label",
      "create",
      label,
      "--repo",
      context.repo,
      "--color",
      LABEL_COLOR,
      "--description",
      LABEL_DESCRIPTION,
      "--force",
    ],
    `gh label create ${label}`,
  )
  context.labels.add(label)
}

/// `gh issue create` echoes the new issue's URL; that is the number.
function numberFromUrl(stdout: string): number {
  const url = stdout.trim().split("\n").at(-1) ?? ""
  const match = /\/issues\/(\d+)\s*$/.exec(url)
  if (match === null) throw new Error(`gh did not report the issue number: ${url}`)
  return Number(match[1])
}

/// A `Gh` backed by the `gh` CLI against `repo`. Needs a token with `repo` scope (`gh auth login`
/// with the default scopes is enough); a refusal propagates as a thrown sweep error, which keeps
/// the watermark where it was.
export function ghCli(repo: string): Gh {
  const context: GhContext = { repo, labels: new Set() }
  return {
    async openIssues(label: string): Promise<IssueRef[]> {
      const out = await orThrow(
        [
          "gh",
          "issue",
          "list",
          "--repo",
          repo,
          "--label",
          label,
          "--state",
          "open",
          "--limit",
          "500",
          "--json",
          "number,title",
        ],
        "gh issue list",
      )
      return (JSON.parse(out) as IssueRef[]).map(({ number, title }) => ({ number, title }))
    },
    async createIssue(title: string, body: string, label: string): Promise<number> {
      await ensureLabel(context, label)
      const path = await bodyFile(body)
      try {
        const out = await orThrow(
          [
            "gh",
            "issue",
            "create",
            "--repo",
            repo,
            "--title",
            title,
            "--label",
            label,
            "--body-file",
            path,
          ],
          `gh issue create "${title}"`,
        )
        return numberFromUrl(out)
      } finally {
        await Deno.remove(path).catch(() => {})
      }
    },
    async commentIssue(number: number, body: string): Promise<void> {
      const path = await bodyFile(body)
      try {
        await orThrow(
          [
            "gh",
            "issue",
            "comment",
            String(number),
            "--repo",
            repo,
            "--body-file",
            path,
          ],
          `gh issue comment ${number}`,
        )
      } finally {
        await Deno.remove(path).catch(() => {})
      }
    },
  }
}

/// The repo the sweep files against: the fork, not upstream (ADR-0005). `SWEEP_REPO` overrides it,
/// mostly so a rehearsal can point at a scratch repo.
export function repoFromEnv(): string {
  return Deno.env.get("SWEEP_REPO") || "wavewarrior/Cataclysm-BN-Forked"
}
