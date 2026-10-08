import { assert, assertEquals, assertStringIncludes } from "@std/assert"
import { evaluateToolCall, type GuardContext, pathsOfToolCall } from "./guard_rules.ts"

const SHA = "a".repeat(40)
const impl: GuardContext = {
  cwd: "C:/WORK/GIT_REPOS/wt-factory-7",
  gitDir: "C:/WORK/GIT_REPOS/Cataclysm-BN-Forked/.git/worktrees/wt-factory-7",
  role: "implementer",
  ticket: "7",
}
const rev: GuardContext = { ...impl, role: "reviewer" }

const bash = (command: string, ctx = impl) =>
  evaluateToolCall({ toolName: "bash", input: { command } }, ctx)
const write = (toolName: string, path: string, ctx = impl) =>
  evaluateToolCall({ toolName, input: { path } }, ctx)

Deno.test("dangerous git and gh commands are blocked with the rule named", () => {
  for (
    const cmd of [
      "git push origin HEAD",
      "git -C . push",
      "git reset --hard HEAD~1",
      "git clean -fd",
      "git branch -D x",
      "git commit --no-verify -m x",
      "git -c core.hooksPath=/dev/null commit",
      "gh pr merge 1",
      "gh.exe api repos/x",
      "echo hi && gh issue close 3",
      "curl https://api.github.com/repos/x",
    ]
  ) assertStringIncludes(bash(cmd) ?? "", "factory-guard:", cmd)
})

Deno.test("ordinary read and build commands are allowed", () => {
  for (
    const cmd of [
      "git status --short",
      "git log --oneline -5",
      "git diff origin/feature/improvements...HEAD -- AGENTS.md",
      "deno task gate --tier fast 2>&1",
      "cat AGENTS.md 2>&1",
      "rg ugh src/",
      "git grep gh -- src",
    ]
  ) assertEquals(bash(cmd), undefined, cmd)
})

Deno.test("a write verb aimed at a protected path is blocked, a read of it is not", () => {
  assertStringIncludes(bash("echo x >> .clang-tidy") ?? "", "protected path")
  assertStringIncludes(bash("sed -i s/a/b/ tools\\factory\\gate.ts") ?? "", "protected path")
  assertStringIncludes(bash("rm -rf .githooks") ?? "", "protected path")
  assertEquals(bash("rg modernize .clang-tidy"), undefined)
  assertEquals(bash("echo x > src/foo.cpp"), undefined)
})

Deno.test("edits to protected paths, outside the worktree and into .git are blocked", () => {
  assert(write("edit", ".clang-tidy"))
  assert(write("write", "tools/factory/config.ts"))
  assert(write("edit", "./build-scripts/lint-json.sh"))
  assert(write("write", "src/../deno.jsonc"))
  assert(write("write", "C:/WORK/GIT_REPOS/other/x.cpp"))
  assert(write("write", "C:/WORK/GIT_REPOS/wt-factory-70/x.cpp"), "sibling dir with same prefix")
  assert(write("write", ".git/hooks/pre-push"))
  assertEquals(write("edit", "src/game.cpp"), undefined)
  assertEquals(write("write", "C:/WORK/GIT_REPOS/wt-factory-7/tests/new_test.cpp"), undefined)
  assertEquals(write("edit", "tools/bnplay/x.ts"), undefined)
})

Deno.test("stamps and verdicts under the git dir are driver-owned for the implementer", () => {
  assert(write("write", `${impl.gitDir}/factory/gate-stamp.json`))
  assert(write("write", `${impl.gitDir}/factory/review-${SHA}.md`))
})

Deno.test("hashline edit headers and MV targets count as touched paths", () => {
  const input = "[tools/factory/gate.ts#AB12]\nPUT 1.=1:\n+x\n[src/a.cpp#CD34]\nMV .clang-tidy\n"
  assertEquals(pathsOfToolCall({ toolName: "edit", input: { input } }), [
    "tools/factory/gate.ts",
    "src/a.cpp",
    ".clang-tidy",
  ])
  assertStringIncludes(
    evaluateToolCall({ toolName: "edit", input: { input } }, impl) ?? "",
    "protected",
  )
  const ok = "[src/a.cpp#CD34]\nPUT 1.=1:\n+x\n"
  assertEquals(evaluateToolCall({ toolName: "edit", input: { input: ok } }, impl), undefined)
})

Deno.test("the reviewer can create exactly its review file and nothing else", () => {
  const review = `${rev.gitDir}/factory/review-${SHA}.md`
  assertEquals(write("write", review, rev), undefined)
  assert(write("write", `${rev.gitDir}/factory/review-${SHA}.md.bak`, rev))
  assert(write("write", `${rev.gitDir}/factory/review-short.md`, rev))
  assert(write("write", `${rev.gitDir}/factory/gate-stamp.json`, rev))
  assert(write("write", `${rev.gitDir}/factory/logs/review-${SHA}.md`, rev))
  assert(write("write", "src/game.cpp", rev))
  assert(write("edit", "src/game.cpp", rev))
  assert(write("ast_edit", "src/game.cpp", rev))
})

Deno.test("the reviewer is read-only in the shell and cannot use eval", () => {
  assertEquals(bash("git diff origin/feature/improvements...HEAD", rev), undefined)
  assertEquals(bash("rg trailing src/ 2>&1 | head -5", rev), undefined)
  assert(bash("echo x > notes.txt", rev))
  assert(bash("git commit -am x", rev))
  assert(bash("git checkout main", rev))
  assert(evaluateToolCall({ toolName: "eval", input: { code: "print(1)" } }, rev))
})

Deno.test("eval code gets the same shell rules as bash for the implementer", () => {
  const call = (code: string) => evaluateToolCall({ toolName: "eval", input: { code } }, impl)
  assert(call("import subprocess; subprocess.run('git push origin HEAD')"))
  assert(call("open('.clang-tidy','w').write('x')"))
  assertEquals(call("print(open('src/a.cpp').read()[:10])"), undefined)
})
