import { assert, assertEquals, assertRejects } from "@std/assert"
import {
  orderDrafts,
  publishDrafts,
  renderBody,
  type TicketDraft,
  validateDrafts,
} from "./publish.ts"
import { dependsOn, makeTicket, missingSections } from "./ticket.ts"

const draft = (key: string, patch: Partial<TicketDraft> = {}): TicketDraft => ({
  key,
  title: `feat(x): ${key}`,
  goal: "Do the thing.",
  acceptance: "It works.",
  touches: ["src/a.cpp"],
  testTags: ["[a]"],
  ...patch,
})

Deno.test("a well-formed breakdown validates and a rendered body parses back as a complete ticket", () => {
  const b = draft("b", { dependsOn: ["a"], episodes: ["t.trial.toml"], noTestNeeded: "docs only" })
  assertEquals(validateDrafts([draft("a"), b]), [])
  const body = renderBody(b, { parent: 9, numberOf: () => 41 })
  const t = makeTicket({ number: 1, title: b.title, body, labels: [] })
  assertEquals(missingSections(t.sections), [])
  assertEquals(dependsOn(t.sections), [41])
  assert(body.startsWith("Part of #9."))
})

Deno.test("each way a ticket would be blocked later is rejected before anything is created", () => {
  const cases: [TicketDraft[], string][] = [
    [[draft("a", { title: "do a thing" })], "conventional"],
    [[draft("a", { touches: [] })], "touches no paths"],
    [[draft("a", { testTags: [] })], "no test tags"],
    [[draft("a", { acceptance: "  " })], "acceptance is empty"],
    [[draft("a", { touches: ["tools/factory/gate.ts"] })], "protected path"],
    [[draft("a", { touches: [".github/workflows"] })], "protected path"],
    [[draft("a", { labels: ["render"] })], "no episodes"],
    [[draft("a", { dependsOn: ["zzz"] })], "unknown key"],
    [[draft("a"), draft("a")], "duplicate key"],
  ]
  for (const [drafts, expected] of cases) {
    assert(validateDrafts(drafts).join("\n").includes(expected), expected)
  }
  assertEquals(validateDrafts([draft("a", { dependsOn: ["#12"] })]), [])
})

Deno.test("blockers are ordered first and a cycle is detected", () => {
  const order = orderDrafts([
    draft("c", { dependsOn: ["b"] }),
    draft("b", { dependsOn: ["a"] }),
    draft("a"),
  ])
  assertEquals(order?.map((d) => d.key), ["a", "b", "c"])
  assertEquals(
    orderDrafts([draft("a", { dependsOn: ["b"] }), draft("b", { dependsOn: ["a"] })]),
    undefined,
  )
  assert(
    validateDrafts([draft("a", { dependsOn: ["b"] }), draft("b", { dependsOn: ["a"] })]).join()
      .includes("cycle"),
  )
})

Deno.test("publishing creates blockers first and writes their real issue numbers into Depends on", async () => {
  const created: { title: string; body: string; labels: string[] }[] = []
  let next = 100
  const made = await publishDrafts({
    drafts: [
      draft("b", { dependsOn: ["a"] }),
      draft("a", { labels: ["gameplay"], episodes: ["e.toml"] }),
    ],
    slug: "feat",
    parent: 7,
    create: (issue) => {
      created.push(issue)
      return Promise.resolve({ number: next++, url: `https://x/${next - 1}` })
    },
  })
  assertEquals(made.map((m) => [m.key, m.number]), [["a", 100], ["b", 101]])
  assertEquals(created[0].labels, ["factory:draft", "spec:feat", "gameplay"])
  assert(created[1].body.includes("## Depends on\n\n- #100"))
})

Deno.test("an invalid breakdown publishes nothing", async () => {
  let calls = 0
  await assertRejects(
    () =>
      publishDrafts({
        drafts: [draft("a"), draft("b", { touches: ["AGENTS.md"] })],
        slug: "s",
        create: () => {
          calls++
          return Promise.resolve({ number: 1, url: "" })
        },
      }),
    Error,
    "protected path",
  )
  assertEquals(calls, 0)
})
