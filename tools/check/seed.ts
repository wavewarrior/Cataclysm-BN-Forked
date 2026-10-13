/// Offline seeding of a fresh worktree's configure.
///
/// A lane's first configure must not hit the network: an unseeded configure stalls on the
/// `git clone` of a FetchContent dependency (measured 2026-10-13: 5+ minutes at 0 KB/s on
/// shadercross's SPIRV-Tools submodule). Pointing `FETCHCONTENT_SOURCE_DIR_<NAME>` at a copy of the
/// sources from an existing build and declaring FetchContent disconnected makes the same configure
/// finish in about 50 s with no network.
///
/// Those sources are not read-only (a build writes into them) and hard links do not isolate a lane,
/// so every lane gets its own `cp -a` copy and is pointed at that copy, never at the seed.

/// The `_deps` entries of a build directory that are FetchContent source trees.
export function seedSourceDirs(entries: readonly string[]): string[] {
  return entries.filter((e) => e.endsWith("-src")).sort()
}

/// `-D` arguments for `cmake --preset linux-fast`: one source dir per dependency, all under
/// `depsDir`, plus the flag that stops FetchContent from contacting a remote.
export function seedArguments(depsDir: string, entries: readonly string[]): string[] {
  const args = seedSourceDirs(entries).map((entry) => [
    `-DFETCHCONTENT_SOURCE_DIR_${entry.slice(0, -"-src".length).toUpperCase()}=${depsDir}/${entry}`,
  ]).flat()
  args.push("-DFETCHCONTENT_FULLY_DISCONNECTED=ON")
  return args
}
