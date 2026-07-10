---
name: fix-pre-existing-before-proceeding
description: "Fix pre-existing bugs and test failures completely before continuing with other work"
condition: ["the crash is fixed.*separate issue", "pre-existing.*not related.*let me", "unrelated to.*proceed", "pre-existing.*move on", "not.*our.*change.*let me", "SIGSEGV is fixed.*separate"]
scope: "text"
---

If you encounter a pre-existing bug or test failure that blocks your work, fix it completely before proceeding. A failing test is a blocking issue regardless of whether the root cause predates your changes. Do not partially fix a crash and leave assertions failing. Do not label something 'separate' or 'pre-existing' as justification to skip it. The test must pass fully before you move to the next task. If the fix requires understanding unfamiliar code, invest the time to understand and fix it properly rather than working around it.