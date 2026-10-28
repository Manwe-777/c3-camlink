# Contributing

## Commit messages

Every commit follows [Conventional Commits](https://www.conventionalcommits.org/):

```
type(scope): summary in the imperative, lower case, no full stop

Why the change was needed and what it does, wrapped at 72 columns. The body
is where the reasoning goes; the header only has to say what changed.
```

The type is not decoration. The release workflow reads it to decide the next
version and to write the release notes, so a commit with the wrong type either
ships without a version bump or bumps the version for nothing.

| Type | Use it for | Release |
|---|---|---|
| `feat` | something a user can now do that they could not before | minor (1.2.0 to 1.3.0) |
| `fix` | something that behaved wrongly and now behaves rightly | patch (1.2.0 to 1.2.1) |
| `perf` | the same behaviour, faster or smaller | patch |
| `refactor` | firmware restructured with no change in behaviour | patch, because the image changes |
| `docs` | documentation only | none |
| `test` | host tests only | none |
| `build` | CMake, sdkconfig defaults, the web toolchain | none |
| `ci` | the workflows under `.github/` | none |
| `chore` | anything else that ships nothing | none |
| `style` | formatting only | none |
| `revert` | undoing an earlier commit | patch |

A change that breaks something a user relies on (a settings format that no
longer loads, a removed console verb, a flashing step that changed) is marked
with `!` after the type, or a `BREAKING CHANGE:` footer explaining what to do.
Either one makes the next release a major version.

The scope is optional and names the part of the tree: `osd`, `gopro`, `dji`,
`msp`, `webcfg`, `usbcli`, `web`, `board`, `ci`, and so on. Lower case.

```
fix(osd): keep the battery glyph on 1S packs
feat(gopro): start recording on arm for the HERO 13
refactor(boot): consume every one-shot flag through one helper
ci(pages): give the deploy job the compiler
feat(webcfg)!: store camera profiles per MAC

BREAKING CHANGE: profiles saved by 1.x are not read. Re-bind the camera
after updating.
```

Do not put the version in the message, and do not edit `VERSION` by hand. The
release workflow writes both.

### The check

`tools/check_commit_msg.sh` enforces the header: a known type, a lower-case
scope if there is one, a summary after `: `, no more than 72 characters, and
no full stop. CI runs it over every commit a push or pull request adds. To run
it locally before each commit, enable the hook once per clone:

```bash
git config core.hooksPath .githooks
```
