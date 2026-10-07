---
name: grilling
description: Grill the user relentlessly about a plan, decision, or idea. Use when the user wants to stress-test their thinking, or uses any 'grill' trigger phrases.
---

Interview the user relentlessly until you reach a shared understanding. Map this as a **design tree**: every decision branches into the decisions that hang off it.

Work the tree in **rounds**. The **frontier** is every decision whose prerequisites are already settled: the questions you can ask _now_ without guessing at answers you haven't heard yet. Ask the whole frontier in one round, then wait for the user's answers before the next round.

**Put each round to the user with the `ask` tool, never as numbered chat text.** One `ask` call per round, one entry in `questions` per frontier question (a short `header`, a self-contained `question`, 2-5 `options` whose `description` states the tradeoff, and `recommended` set to your recommended answer). Do not paste the questions, the options, or "my recommendation" into the chat reply as well; the prompt is the question. Put background in the `question` text only as far as the user needs to choose, and keep the facts you found short and cited (`file:line`). The user can always answer "Other (type your own)", so never add an "Other" option.

**Before you ask, every named thing must be real.** A question may only refer to a flag, function, call site, or number you have verified in the code or tool output (open the lines, or have a sub-agent report `file:line`). Never write "and one other", "about a dozen", or "probably identical": find the exact item and name it, or leave that question for a later round until the sub-agent reports. A scout's summary is a lead, not a fact: open the cited lines for any claim a decision will rest on.

Each round the user answers reshapes the tree: settled decisions push the frontier outward and unblock questions that depended on them. Recompute the frontier and ask the next round. A question whose answer depends on another question still open in this round belongs to a _later_ round, not this one.

Finding _facts_ is your job, never the user's. When a frontier question needs a fact from the environment (filesystem, tools, etc.), dispatch a sub-agent to find it; don't ask the user for anything you could look up yourself. Don't block on it: a running exploration is an unsettled prerequisite, so only the questions downstream of it wait for the sub-agent to report; ask the rest of the frontier now. The _decisions_ are the user's: put each to them and wait.

The session is done when the frontier is empty: every branch of the design tree visited, nothing left silently assumed. Do not act on it until the user confirms you have reached a shared understanding.
