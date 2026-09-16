# Syncing this fork with upstream Wayfire

A runbook for pulling upstream changes into this fork without silently breaking the desktop.

> **Read this first:** Wayfire's plugin ABI is stamped with a **date**, not a version number.
> Syncing upstream changes that stamp, and the compositor then refuses to load *any* plugin
> compiled against the old one — with no crash and no error dialog. The plugin simply ceases to
> exist. **Every sync therefore requires rebuilding both repos, not just this one.**

## The two stacks on this machine

| Prefix | What | Role |
|---|---|---|
| `/usr` | pacman `wayfire 0.10.x`, `wayfire-plugins-extra`, `wf-config 0.10.0` | **TTY fallback. Never touch it.** |
| `/usr/local` | this repo (upstream master, 0.12-dev as of 2026-09) + its `wf-config`, + `~/Documents/Projects/wayfire-plugins-extra` | daily driver |

Because both stacks are installed, `pkg-config` will happily resolve to the *wrong* one. Every
out-of-tree build below must set `PKG_CONFIG_PATH=/usr/local/lib/pkgconfig` so it links against
this repo's Wayfire and `wf-config` rather than the pacman 0.10.0. Omitting it yields a clean
build that produces unloadable plugins.

## One-time setup

```sh
git remote add upstream https://github.com/WayfireWM/wayfire.git
git clone https://github.com/WayfireWM/wayfire-plugins-extra.git ~/Documents/Projects/wayfire-plugins-extra
```

`origin` is the fork (pushable); `upstream` is the original (read-only).

## The sync

### 1. Merge upstream into this repo

```sh
git checkout master
git branch -f backup/pre-upstream-merge master      # cheap undo
git fetch upstream
git merge upstream/master
```

**Merge, not rebase.** `master` is already published to `origin`; rebasing would rewrite those
commits and force a `git push --force`. Merge keeps a plain push working. (Rebase becomes the
right tool only if this fork ever targets an upstream pull request.)

`README.md` conflicts on most syncs — this fork replaced it with a portfolio page while upstream
keeps editing theirs. The resolution is always the same:

```sh
git checkout --ours README.md && git add README.md && git commit --no-edit
```

> During a **merge**, `--ours` is your branch. During a **rebase** the meaning inverts, because
> rebase replays your commits *onto* upstream. Read `git status` if unsure.

Conflicts are otherwise rare by construction: custom work lives in new directories
(`plugins/spread-overview/`, `specs/`, `docs/adr/`) and only *appends* to shared files like
`plugins/meson.build`, which git merges automatically.

The exception is a short shared file where upstream appends **at the same spot**. On the
2026-09-16 sync `test/plugins/meson.build` conflicted: upstream added `subdir('common')` /
`subdir('vswitch')` right where the fork's spread-overview layout test is appended. Both sides are
additive, so the resolution is to keep both — upstream's lines first, the fork's block after.

### 2. Update submodules

```sh
git submodule update --recursive
```

Submodules are recorded as commit *pointers*. A merge moves the pointer but leaves the checked-out
files stale, so skipping this builds new Wayfire against old `wlroots`/`wf-config` headers.

Note the omitted `--init`: `subprojects/wlroots-vkfx` is intentionally left uninitialized.

**Version bumps move `wf-config` too.** When upstream bumps its own version (e.g. `0.11.0` →
`0.12.0` in `meson.build`), it also bumps the required `wf-config` range. With
`use_system_wfconfig=auto`, configure then rejects the previously installed `/usr/local` copy
("Found 0.11.0 but need >=0.12.0"), falls back to the `subprojects/wf-config` submodule, and
installs *that* into `/usr/local` in step 3. This is expected, not an error — and it is why step 4
must follow, since plugins-extra links against `wf-config` as well.

### 3. Rebuild and install Wayfire

```sh
ninja -C build && sudo ninja -C build install
```

In-tree plugins (`spread-overview`) rebuild automatically here — that is the ongoing payoff of
the in-tree decision.

### 4. Rebuild `wayfire-plugins-extra` — do not skip

```sh
cd ~/Documents/Projects/wayfire-plugins-extra
git pull
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig meson setup build --prefix=/usr/local --wipe
ninja -C build && sudo meson install -C build --no-rebuild
```

Use `--wipe` (or `--reconfigure`) so the cached dependency paths are re-resolved against the new
Wayfire; on a first-time setup, drop it. `--wipe` re-applies the options originally passed on the
command line, so if an option is ever renamed upstream, drop the build directory instead.

The optional add-ons (`pixdecor`, `wayfire-shadows`, `filters`, `focus-request`) are deliberately
**off**. Since plugins-extra 0.11.2 they are meson `.wrap` files (fetched from git at configure
time) behind boolean options named `pixdecor`, `wayfire_shadows`, `filters`, `focus_request` —
formerly git submodules behind `enable_*` options. All default to `false`, so the reconfigure above
downloads nothing; passing e.g. `-Dpixdecor=true` would clone that add-on on the next configure.

Deprecation warnings during this build are expected — upstream deprecates APIs that plugins-extra
has not migrated yet. Only errors matter.

## Verify before restarting

Every plugin exports `getWayfireVersion()`, which returns the ABI stamp it was compiled against.
This reads it back out of every installed `.so` and compares against what the compositor expects:

```sh
cd /usr/local/lib/wayfire
for so in *.so; do
  objdump -d --disassemble=getWayfireVersion "$so" 2>/dev/null | grep -oE '0x[0-9a-f]+' | head -1
done | sort -u | while read v; do echo "plugins: $((v))"; done
grep pluginabi /usr/local/lib/pkgconfig/wayfire.pc
```

**Pass = exactly one `plugins:` line, and it equals `pluginabi`.** Two or more lines means some
plugins are stale and will silently fail to load. This beats comparing file timestamps, which
only tells you when a file was written, not what it was built against.

Then run the tests and verify live from a throwaway TTY:

```sh
meson test -C build "Spread overview layout test"
```

## Rollback

```sh
git reset --hard backup/pre-upstream-merge
git submodule update --recursive
ninja -C build && sudo ninja -C build install
```

Then rebuild plugins-extra at its matching older commit. Delete the backup branch once the sync
is confirmed good.

## Publish

```sh
git push origin master
```

GitHub keeps reporting "N commits behind" until this runs — the counter is computed server-side.
No `--force` needed, which is precisely why step 1 uses merge.

## Cadence

Sync every few weeks. Each sync is then one trivial `README.md` conflict. Left for months, it
becomes an API-reconciliation project across hundreds of commits.

## Symptom → cause

| Symptom | Cause |
|---|---|
| A plugin silently stops working (e.g. focus stops following the mouse) | Stale ABI — rerun step 4 |
| Build errors about `wf-config` types or missing headers | `PKG_CONFIG_PATH` unset → linked the pacman 0.10.0 |
| Wayfire builds but behaves oddly at the render layer | Submodules not updated (step 2), especially `wlroots` |
