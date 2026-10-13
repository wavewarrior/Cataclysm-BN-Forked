/// Deno lint plugin `repo-rules`: closes the two cheapest ways an agent can turn a gate green.
///   repo-rules/no-skipped-tests       a test that is skipped, ignored or focused with `only`
///   repo-rules/no-unreasoned-suppress a suppression comment without ` -- <reason>`
/// Enabled from deno.jsonc (`lint.plugins`, `lint.rules.include`).

const REGISTRARS = new Set(["describe", "it", "test"])

/// `Deno.test`, `describe`, `it`, `test`, or `t.step`-style member chains that register a test.
function isRegistrar(node: Deno.lint.Node): boolean {
  if (node.type === "Identifier") return REGISTRARS.has(node.name)
  if (node.type !== "MemberExpression" || node.computed) return false
  if (node.property.type !== "Identifier") return false
  if (node.object.type === "Identifier" && node.object.name === "Deno") {
    return node.property.name === "test"
  }
  return node.property.name === "step"
}

const SUPPRESS = /^\s*(?:@ts-ignore|@ts-expect-error|@ts-nocheck|deno-lint-ignore(?:-file)?\b)(.*)$/

const plugin: Deno.lint.Plugin = {
  name: "repo-rules",
  rules: {
    "no-skipped-tests": {
      create(ctx) {
        return {
          MemberExpression(node) {
            if (node.computed || node.property.type !== "Identifier") return
            const name = node.property.name
            if (
              (name === "ignore" || name === "skip" || name === "only") && isRegistrar(node.object)
            ) {
              ctx.report({ node, message: `Do not use .${name} on a test: fix it or delete it.` })
            }
          },
          CallExpression(node) {
            if (!isRegistrar(node.callee)) return
            for (const arg of node.arguments) {
              if (arg.type !== "ObjectExpression") continue
              for (const prop of arg.properties) {
                if (
                  prop.type === "Property" && !prop.computed && prop.key.type === "Identifier" &&
                  (prop.key.name === "ignore" || prop.key.name === "only") &&
                  !(prop.value.type === "Literal" && prop.value.value === false)
                ) {
                  ctx.report({ node: prop, message: `Do not set { ${prop.key.name} } on a test.` })
                }
              }
            }
          },
        }
      },
    },
    "no-unreasoned-suppress": {
      create(ctx) {
        return {
          Program() {
            for (const comment of ctx.sourceCode.getAllComments()) {
              const m = comment.value.match(SUPPRESS)
              if (!m) continue
              // `deno-lint-ignore rule-a rule-b -- reason`: the reason is whatever follows ` -- `.
              const reason = m[1].split(/\s--\s/)[1]?.trim() ?? ""
              if (reason.length === 0) {
                ctx.report({
                  range: comment.range,
                  message: "A suppression needs a reason: add ` -- <why this is safe>`.",
                })
              }
            }
          },
        }
      },
    },
  },
}

export default plugin
