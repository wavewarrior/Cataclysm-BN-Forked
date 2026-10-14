import { assertEquals } from "@std/assert"
import { crashExcerpt, parseCatchXml } from "./report.ts"

const HEADER = `<?xml version="1.0" encoding="UTF-8"?>
<Catch name="cata_test-tiles">
  <Group name="cata_test-tiles">`
const FOOTER = `  </Group>
  <OverallResults successes="1" failures="1" expectedFailures="0" skips="0"/>
</Catch>
`

Deno.test("a passing case is not a failure; a failed OverallResult is", () => {
  const xml = HEADER +
    `    <TestCase name="a good case" tags="[good]">
      <OverallResult success="true" skips="0"/>
    </TestCase>
    <TestCase name="a bad case" tags="[bad]">
      <Expression success="false" type="REQUIRE" filename="tests/bad_test.cpp" line="42">
        <Original>x == 2</Original>
        <Expanded>x == 1</Expanded>
      </Expression>
      <OverallResult success="false" skips="0"/>
    </TestCase>
  ` + FOOTER
  const { failures, crashed } = parseCatchXml(xml)
  assertEquals(crashed, null)
  assertEquals(failures.length, 1)
  assertEquals(failures[0].name, "a bad case")
  assertEquals(failures[0].excerpt.includes("REQUIRE FAILED (tests/bad_test.cpp:42): x == 2"), true)
  assertEquals(failures[0].excerpt.includes("expanded: x == 1"), true)
})

Deno.test("exact case names containing a slash survive, which junit cannot promise", () => {
  const xml = HEADER +
    `    <TestCase name="Radius one 2D square centered at -4/-4/0." tags="">
      <OverallResult success="false" skips="0"/>
    </TestCase>
  ` + FOOTER
  assertEquals(parseCatchXml(xml).failures[0].name, "Radius one 2D square centered at -4/-4/0.")
})

Deno.test("entities in a name come back decoded", () => {
  const xml = HEADER +
    `    <TestCase name="healing_rate_medicine with bandages and&#47;or disinfectant" tags="">
      <OverallResult success="false" skips="0"/>
    </TestCase>
  ` + FOOTER
  assertEquals(
    parseCatchXml(xml).failures[0].name,
    "healing_rate_medicine with bandages and/or disinfectant",
  )
})

Deno.test("an exception failure becomes an excerpt line", () => {
  const xml = HEADER +
    `    <TestCase name="throws" tags="">
      <Exception filename="tests/throw_test.cpp" line="7">
        std::runtime_error: boom
      </Exception>
      <OverallResult success="false" skips="0"/>
    </TestCase>
  ` + FOOTER
  const { failures } = parseCatchXml(xml)
  assertEquals(failures[0].excerpt.includes("Exception (tests/throw_test.cpp:7)"), true)
  assertEquals(failures[0].excerpt.includes("boom"), true)
})

Deno.test("a report that ends mid-case is a crash in that case", () => {
  const xml = HEADER +
    `    <TestCase name="fine" tags="">
      <OverallResult success="true" skips="0"/>
    </TestCase>
    <TestCase name="kills the process" tags="">
      <Expression success="false" type="REQUIRE" filename="tests/crash_test.cpp" line="3">
        <Original>true</Original>
      </Expression>
  `
  const { failures, crashed } = parseCatchXml(xml)
  assertEquals(crashed, "kills the process")
  assertEquals(failures.length, 0)
})

Deno.test("a crash excerpt carries the console tail", () => {
  const log = Array.from({ length: 40 }, (_, i) => `line ${i}`).join("\n")
  const excerpt = crashExcerpt(log, 3)
  assertEquals(excerpt.includes("line 39"), true)
  assertEquals(excerpt.includes("line 37"), true)
  assertEquals(excerpt.includes("line 30"), false)
})
