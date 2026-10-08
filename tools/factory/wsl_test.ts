import { assertEquals } from "@std/assert"
import { toWslPath } from "./wsl.ts"

Deno.test("windows paths map to /mnt and other paths pass through", () => {
  assertEquals(toWslPath("C:\\WORK\\x\\y"), "/mnt/c/WORK/x/y")
  assertEquals(toWslPath("D:/a"), "/mnt/d/a")
  assertEquals(toWslPath("/home/x"), "/home/x")
})
