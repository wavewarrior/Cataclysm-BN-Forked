import { assertEquals } from "@std/assert"
import { lineFilterFromDiff, toWslPath } from "./wsl.ts"

const DIFF = `diff --git a/src/a.cpp b/src/a.cpp
--- a/src/a.cpp
+++ b/src/a.cpp
@@ -10,0 +11,3 @@ void x()
+a
+b
+c
@@ -40,2 +43 @@ void y()
-old1
-old2
+new
@@ -50,2 +53,0 @@ void z()
-gone1
-gone2
diff --git a/data/x.json b/data/x.json
--- a/data/x.json
+++ b/data/x.json
@@ -1 +1 @@
-a
+b
diff --git a/src/b.h b/src/b.h
--- a/src/b.h
+++ /dev/null
@@ -1,2 +0,0 @@
-x
-y
diff --git a/tests/c.cpp b/tests/c.cpp
new file mode 100644
--- /dev/null
+++ b/tests/c.cpp
@@ -0,0 +1,4 @@
+1
+2
+3
+4
`

Deno.test("the line filter keeps only added or changed C++ lines", () => {
  assertEquals(lineFilterFromDiff(DIFF), [
    { name: "src/a.cpp", lines: [[11, 13], [43, 43]] },
    { name: "tests/c.cpp", lines: [[1, 4]] },
  ])
})

Deno.test("an empty diff yields an empty filter", () => {
  assertEquals(lineFilterFromDiff(""), [])
})

Deno.test("windows paths map to /mnt and other paths pass through", () => {
  assertEquals(toWslPath("C:\\WORK\\x\\y"), "/mnt/c/WORK/x/y")
  assertEquals(toWslPath("D:/a"), "/mnt/d/a")
  assertEquals(toWslPath("/home/x"), "/home/x")
})
