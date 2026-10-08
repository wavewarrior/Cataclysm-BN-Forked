import { assertEquals } from "@std/assert"
import {
  branchName,
  dependsOn,
  listItems,
  makeTicket,
  missingSections,
  parseSections,
} from "./ticket.ts"

const BODY = `## Goal
Make the thing work.

## Acceptance
- it works

## Touches
- src/a.cpp

## Test tags
- [rot]
\`[vehicle]\`

## Depends on
- #12
- owner/repo#7

## Unrelated heading
ignored
`

Deno.test("sections are split on level-2 headings and unknown headings are dropped", () => {
  const s = parseSections(BODY)
  assertEquals(s["Goal"], "Make the thing work.")
  assertEquals(Object.keys(s), ["Goal", "Acceptance", "Touches", "Test tags", "Depends on"])
  assertEquals(s["Depends on"]?.includes("ignored"), false)
})

Deno.test("heading match is case-insensitive and CRLF bodies parse", () => {
  assertEquals(parseSections("## test TAGS\r\n[rot]\r\n")["Test tags"], "[rot]")
})

Deno.test("a blank required section counts as missing", () => {
  const t = makeTicket({
    number: 1,
    title: "x",
    labels: [],
    body: "## Goal\ng\n\n## Acceptance\n\n",
  })
  assertEquals(missingSections(t.sections), ["Acceptance", "Touches", "Test tags"])
})

Deno.test("list items drop bullets, backticks and html comments", () => {
  assertEquals(listItems("- [rot]\n`[vehicle]`\n<!-- note -->\n\n1. [map]"), [
    "[rot]",
    "[vehicle]",
    "[map]",
  ])
})

Deno.test("dependencies accept bare, hashed and qualified issue numbers", () => {
  assertEquals(dependsOn(parseSections(BODY)), [12, 7])
})

Deno.test("branch names strip the conventional-commit prefix and cap the slug", () => {
  assertEquals(
    branchName("factory/", 9, "fix(rot): Corpses never rot!"),
    "factory/9-corpses-never-rot",
  )
  assertEquals(branchName("factory/", 9, "!!!"), "factory/9-ticket")
  assertEquals(branchName("factory/", 9, "a".repeat(80)).length, "factory/9-".length + 40)
})
