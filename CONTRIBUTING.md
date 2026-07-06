# Contributing to Generals-Web

Thanks for wanting to help get a 2003 DirectX RTS running in a browser tab. Whether you
fix one duplicate HTTP fetch or wire up WebRTC multiplayer, you're welcome here.

This project is a fork of [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode)
that adds a WebAssembly/WebGL 2.0 platform layer. Most of what you'll touch lives in
that layer; the inherited engine code follows upstream conventions.

---

## The Three Rules (non-negotiable)

These keep the project legal and alive. Everything else in this document is guidance;
these are requirements.

### 1. GPL-3.0 in, GPL-3.0 out

All code in this repository is licensed under **GPL-3.0 with EA's additional terms**
(see [LICENSE.md](LICENSE.md) — never modify that file). By submitting a contribution,
you agree it is licensed the same way. Don't submit code you can't license under
GPL-3.0: no proprietary SDK code, no code under incompatible licenses, no
copy-pasted snippets of unknown provenance. Keep existing EA and TheSuperHackers
copyright headers intact.

New files get a short header:

```
// Part of Generals-Web. Derived from EA's C&C Generals — Zero Hour source (GPL-3.0).
// SPDX-License-Identifier: GPL-3.0-only
```

### 2. Never submit EA game assets — anywhere

No `.big` or `.bik` archives, no game textures, audio, models, or maps. Not in pull
requests, not in issue attachments, not as test fixtures, not "just this one small
INI file". The game data belongs to EA and is not redistributable; a single committed
archive would force a history rewrite. CI (`asset-guard`) fails loudly if game data
appears in a change. Screenshots of the running game are fine — the game's *files*
are not. See [NOTICE](NOTICE) for the full legal position.

### 3. No code reverse-engineered from retail binaries

Everything here descends from EA's official GPL source release via
GeneralsGameCode. Code derived from disassembling or decompiling retail game
executables (outside that GPL lineage) cannot be accepted. If you're
reimplementing behavior, work from the GPL source, public documentation, or
clean-room observation of the game as a player.

---

## Getting Started

1. **Build it.** Follow [BUILDING.md](BUILDING.md) — the full path from clone to a
   main menu in your browser. Expect the first build to take 20–40 minutes.
2. **Pick something.** [docs/web-port/GOOD-FIRST-ISSUES.md](docs/web-port/GOOD-FIRST-ISSUES.md)
   is a curated list of real, scoped tasks with file anchors and first steps — from
   one-evening documentation fixes to the WebRTC moonshot. The live versions are on
   the [issue tracker](../../issues) under the `good first issue` and `help wanted` labels.
3. **Read the lore.** [docs/web-port/PORTING_LOG.md](docs/web-port/PORTING_LOG.md) is the
   session-by-session engineering diary of the port. It will save you from re-debugging
   things that already ate someone's weekend.

---

## Document Your Session (the porting-log convention)

This project's most valuable artifact after the code is the porting log. If your change
came out of a real porting or debugging session — not a typo fix, but the kind of work
where you learned something — append an entry to
[docs/web-port/PORTING_LOG.md](docs/web-port/PORTING_LOG.md):

```markdown
## YYYY-MM-DD — Session N: Short Title
- What you set out to do
- What you found (including dead ends — those count double)
- What you changed, and what's still broken
```

Future contributors mine this log constantly. The dead ends are as valuable as the
fixes.

---

## Pull Request Expectations

- **Verify in-browser before opening the PR.** "It compiles" is not verification for
  this project. Build, serve, load the page, and exercise the thing you changed. In the
  PR description, say exactly what you tested ("boots to main menu", "started a
  skirmish, played 5 minutes") and in which browsers/versions.
- **PR titles follow [conventional commits](https://www.conventionalcommits.org/en/v1.0.0/):**
  `type: Description` or `type(scope): Description`, starting with an uppercase letter.
  Allowed types are listed in [`.github/workflows/valid-tags.txt`](.github/workflows/valid-tags.txt)
  (`fix`, `feat`, `bugfix`, `build`, `docs`, `perf`, `refactor`, ...). CI enforces this.
- **Keep changes scoped.** Touch only the lines that serve the goal of the change.
  Refactors go in separate commits (or separate PRs) from behavior changes.
- **No game assets** (see Rule 2). The PR template has a checkbox; CI checks anyway.
- **Fill in the PR template** — it exists so reviewers don't have to guess.

### Code comment convention

Inherited from upstream, adapted for the fork: annotate meaningful changes at the site
of the change.

- Changes to **web-port code** (the platform layer this fork added):
  `// GeneralsX @keyword author DD/MM/YYYY Description of the change.`
- Changes to **inherited engine code** (anything that exists upstream):
  `// TheSuperHackers @keyword author DD/MM/YYYY Description of the change.`
  (keeping upstream's format makes future upstream merges survivable)

Common keywords: `@bugfix`, `@fix`, `@build`, `@feature`, `@performance`, `@refactor`,
`@tweak`, `@info`, `@todo`.

### Code style

Match the surrounding code. The original engine is C++98-flavored and simple to read;
prefer not to introduce newer language features into engine code unless they make it
considerably more robust or clearer. The web platform layer (`EmscriptenMain.cpp`,
`gles3_*.cpp`, `web_audio_bridge.cpp`, etc.) is more modern — match what's there.

---

## Reporting Bugs and Requesting Features

Use the [issue templates](../../issues/new/choose). For bugs, browser + version,
build type, and console output (`F12` → Console) turn a vague report into a fixable
one. Security issues go through [SECURITY.md](SECURITY.md) instead.

---

## Be Kind

This is a hobby project about a 20-year-old game. People show up with wildly different
experience levels — some know WebGL and have never seen the engine; some know the
engine from the modding days and have never touched a browser API. Both are exactly who
we need. Review generously, ask before assuming, and remember there's a person on the
other end of every diff. The [Code of Conduct](CODE_OF_CONDUCT.md) applies everywhere
this project lives.
