#!/usr/bin/env bash
# Check one commit message against the Conventional Commits header format.
#
#   type(scope)!: summary
#
# The type is what tools/release reads to decide the next version, so a commit
# that does not carry one is a change the release cannot see: it ships in the
# next image without bumping the number or appearing in the notes. That is why
# this is a check and not a suggestion.
#
# Used two ways, both with a file holding the message as the only argument:
#   - as the commit-msg hook (.githooks/commit-msg), before the commit exists
#   - by CI, over every commit a push or pull request adds
#
# No Node, on purpose: Node is a contributor-only dependency of web/, and a
# hook that needs npm install would only run for the people who did one.
set -euo pipefail

msg_file="${1:?usage: check_commit_msg.sh <file holding the message>}"

# First line that is not a git comment. With --cleanup=strip (the default for
# an editor commit) comment lines are removed after the hook runs, not before.
header="$(grep -v '^#' "$msg_file" | sed -n '/./{p;q;}' || true)"

# Git writes these itself, and an autosquash rebase removes the fixup ones
# before they reach main.
case "$header" in
  "Merge "* | "Revert \""* | "fixup! "* | "squash! "* | "amend! "*) exit 0 ;;
esac

types='feat|fix|perf|refactor|docs|test|build|ci|chore|style|revert'
pattern="^(${types})(\([a-z0-9][a-z0-9._/-]*\))?!?: [^ ].*$"

fail() {
  cat >&2 <<EOF
Commit message header does not follow the format:

  ${header:-<empty>}

$1

Expected:  type(scope): summary      e.g.  fix(osd): keep the battery glyph on 1S packs
Types:     ${types//|/, }
Breaking:  add ! before the colon, or a "BREAKING CHANGE:" footer
See CONTRIBUTING.md.
EOF
  exit 1
}

[[ "$header" =~ $pattern ]] || fail "The type is missing, unknown, or not followed by \": \"."
[ "${#header}" -le 72 ] || fail "The header is ${#header} characters; keep it to 72 so git log --oneline stays readable."
[[ "$header" != *. ]] || fail "Drop the full stop at the end of the header."
exit 0
