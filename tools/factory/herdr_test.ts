import { assertEquals } from "@std/assert"
import { worktreeEntries } from "./herdr.ts"

// Shape copied from `herdr worktree list --cwd <repo> --trust-repository`: one entry with an open workspace, one without.
Deno.test("worktreeEntries keeps branch, path and open workspace, and leaves absent ones undefined", () => {
  const entries = worktreeEntries({
    worktrees: [
      {
        branch: "feature/fast-track-to-co-op",
        is_detached: false,
        is_linked_worktree: false,
        path: "C:/WORK/GIT_REPOS/Cataclysm-BN-Forked",
        open_workspace_id: "wD",
      },
      {
        branch: "fix/capture-avatar-determinism",
        is_detached: false,
        is_linked_worktree: true,
        path: "C:/WORK/GIT_REPOS/wt-avatarcycle",
      },
      {
        is_detached: true,
        is_linked_worktree: true,
        path: "C:/WORK/GIT_REPOS/wt-detached",
      },
    ],
  })
  assertEquals(entries, [
    {
      path: "C:/WORK/GIT_REPOS/Cataclysm-BN-Forked",
      branch: "feature/fast-track-to-co-op",
      openWorkspaceId: "wD",
    },
    {
      path: "C:/WORK/GIT_REPOS/wt-avatarcycle",
      branch: "fix/capture-avatar-determinism",
      openWorkspaceId: undefined,
    },
    { path: "C:/WORK/GIT_REPOS/wt-detached", branch: undefined, openWorkspaceId: undefined },
  ])
})
