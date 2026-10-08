import { assertEquals } from "@std/assert"
import plugin from "./lint_plugin.ts"

const lint = (source: string) =>
  Deno.lint.runPlugin(plugin, "sample.ts", source).map((d) => `${d.id}`)

Deno.test("no-skipped-tests flags ignore, only and skip in every registrar form", () => {
  const bad = [
    "Deno.test({ name: 'a', ignore: true, fn() {} })",
    "Deno.test({ name: 'a', only: true, fn() {} })",
    "Deno.test({ name: 'a', ignore: Deno.build.os === 'windows', fn() {} })",
    "Deno.test.ignore('a', () => {})",
    "Deno.test.only('a', () => {})",
    "describe.skip('a', () => {})",
    "it.only('a', () => {})",
    "test.skip('a', () => {})",
  ]
  for (const source of bad) {
    assertEquals(lint(source).includes("repo-rules/no-skipped-tests"), true, source)
  }
})

Deno.test("no-skipped-tests leaves ordinary tests, ignore: false and unrelated .only alone", () => {
  const ok = [
    "Deno.test('a', () => {})",
    "Deno.test({ name: 'a', ignore: false, fn() {} })",
    "Deno.test('a', async (t) => { await t.step('b', () => {}) })",
    "const x = list.only(3); other.skip()",
  ]
  for (const source of ok) assertEquals(lint(source), [], source)
})

Deno.test("no-unreasoned-suppress flags a bare suppression and accepts one with a reason", () => {
  assertEquals(lint("// @ts-ignore\nconst a: number = 'x'"), ["repo-rules/no-unreasoned-suppress"])
  assertEquals(lint("// @ts-expect-error\nconst a: number = 'x'"), [
    "repo-rules/no-unreasoned-suppress",
  ])
  assertEquals(lint("// deno-lint-ignore no-unused-vars\nconst a = 1"), [
    "repo-rules/no-unreasoned-suppress",
  ])
  assertEquals(
    lint("// @ts-expect-error -- zod rejects string literal unions\nconst a: number = 'x'"),
    [],
  )
  assertEquals(lint("// deno-lint-ignore no-unused-vars -- fixture\nconst a = 1"), [])
})

Deno.test("a dash-dash with no text after it is still unreasoned", () => {
  assertEquals(lint("// @ts-ignore --\nconst a = 1"), ["repo-rules/no-unreasoned-suppress"])
})
