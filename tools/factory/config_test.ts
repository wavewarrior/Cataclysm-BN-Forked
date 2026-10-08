import { assertEquals } from "@std/assert"
import { config } from "./config.ts"

Deno.test("config.json is the generated image of config.ts (CI reads the JSON from the base ref)", async () => {
  const json = JSON.parse(await Deno.readTextFile(new URL("./config.json", import.meta.url)))
  assertEquals(json, JSON.parse(JSON.stringify(config)))
})
