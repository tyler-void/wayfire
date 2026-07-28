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
| `/usr/local` | this repo (0.11-dev) + `~/Documents/Projects/wayfire-plugins-extra` | daily driver |

Because both stacks are installed, `pkg-config` will happily resolve to the *wrong* one. Every
out-of-tree build below must set `PKG_CONFIG_PATH=/usr/local/lib/pkgconfig` so it links against
0.11.0 rather than the pacman 0.10.0. Omitting it yields a clean build that produces
unloadable plugins.

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

### 2. Update submodules

```sh
git submodule update --recursive
```

Submodules are recorded as commit *pointers*. A merge moves the pointer but leaves the checked-out
files stale, so skipping this builds new Wayfire against old `wlroots`/`wf-config` headers.

Note the omitted `--init`: `subprojects/wlroots-vkfx` is intentionally left uninitialized.

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
Wayfire; on a first-time setup, drop it. Optional bundled submodules (`pixdecor`,
`wayfire-shadows`, `filters`, `focus-request`) are deliberately **not** initialized.

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
