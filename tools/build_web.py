#!/usr/bin/env python3
"""Assemble both browser front-ends from one set of sources under web/.

WHY THIS EXISTS. There are two front-ends and they are the same page:

  components/webcfg/web/index.html  the settings page served over Wi-Fi by the
                                    module itself, gzipped into the firmware
  dist/index.html                   the same page driven over a USB cable,
                                    served from GitHub Pages

Kept in step by hand they drift, and the drift is invisible: the two files are
never diffed against each other, so a missing endpoint in one of the maps looks
like nothing at all until switching between saved cameras fails over USB and
works over Wi-Fi.

So NEITHER FILE IS EDITED DIRECTLY. Both are generated from web/, which keeps
the shared page in one copy and the USB-only parts beside it:

  web/shared/     the page: CSS, markup, and the settings logic, transport-free
  web/usb/        the Web Serial transport, the connect gate, the esptool panel
  web/types/      the shapes the firmware's JSON actually has
  web/*.html.in   one template per output, listing the fragments in order

THE LOGIC IS TYPESCRIPT. A template that includes `shared/app.js` is asking for
the COMPILED form of `web/shared/app.ts`, which tsc writes to web/.build/. That
indirection is deliberate: templates name the file the browser ends up running,
and only this module knows where it came from. Run `npm run build` in web/ to
do both steps, or `npx tsc` here first -- this script refuses rather than
silently inlining a stale compile.

The outputs are COMMITTED, not built on demand. The firmware build just gzips a
file that is already there, and Pages just serves one, so neither needs Node.
The cost of that is the outputs can go stale, which `--check` catches in CI.

Usage:
    tools/build_web.py            write the outputs
    tools/build_web.py --check    exit 1 if a committed output is stale
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
# Where tsc puts the compiled logic. Gitignored: it is an intermediate, and the
# committed outputs below are what anything downstream actually consumes.
BUILD = WEB / ".build"

# template -> output
TARGETS = {
    "device.html.in": "components/webcfg/web/index.html",
    "usb.html.in": "dist/index.html",
}

INCLUDE = "<!-- include: "


def fragment_path(rel: str) -> Path:
    """Where a template's include actually lives.

    Anything ending .js is compiler output and comes from .build/; everything
    else -- CSS, markup -- is a source file and comes from web/ directly. A
    template therefore names what the browser runs, not where it was built.
    """
    return (BUILD / rel) if rel.endswith(".js") else (WEB / rel)


def render(template: Path, version: str) -> str:
    """Expand the include lines in a template, one level, in order."""
    out = []
    for lineno, line in enumerate(template.read_text(encoding="utf-8").splitlines(True), 1):
        stripped = line.strip()
        if not stripped.startswith(INCLUDE):
            out.append(line)
            continue
        if not stripped.endswith("-->"):
            sys.exit(f"{template.name}:{lineno}: malformed include: {stripped}")
        rel = stripped[len(INCLUDE):-len("-->")].strip()
        frag = fragment_path(rel)
        if not frag.is_file():
            if rel.endswith(".js"):
                sys.exit(f"{template.name}:{lineno}: {frag.relative_to(ROOT)} is "
                         f"missing -- compile the TypeScript first:\n"
                         f"    cd web && npm install && npx tsc")
            sys.exit(f"{template.name}:{lineno}: no such fragment: {rel}")
        text = frag.read_text(encoding="utf-8")
        # Fragments are whole lines and end with a newline, so they drop into
        # the surrounding markup without shifting anything around them.
        if text and not text.endswith("\n"):
            text += "\n"
        out.append(text)

    body = "".join(out).replace("@VERSION@", version)

    # Named after the template rather than just "generated", so someone who
    # opens the wrong file knows which one to open instead.
    banner = (f"<!-- Generated from web/{template.name} by tools/build_web.py."
              f" Do not edit. -->\n")
    doctype, sep, rest = body.partition("\n")
    return doctype + sep + banner + rest


def main() -> int:
    check = "--check" in sys.argv[1:]
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()

    # There used to be a regex here that pulled every /api/ name out of the page
    # and diffed it against the USB transport's verb table, because an endpoint
    # in one and not the other works over Wi-Fi and fails silently over the
    # cable. That is now the `Endpoint` union in web/types/api.d.ts: the
    # transport types its table as Record<Endpoint, string>, so a missing entry
    # does not compile. A type beats a linter that has to be kept in step.

    stale = []
    for name, dest in TARGETS.items():
        text = render(WEB / name, version)
        path = ROOT / dest
        current = path.read_text(encoding="utf-8") if path.is_file() else None
        if check:
            if current != text:
                stale.append(dest)
            continue
        if current == text:
            print(f"unchanged  {dest}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
            print(f"wrote      {dest}  ({len(text.encode())} bytes)")

    if stale:
        print("These generated files do not match web/:", file=sys.stderr)
        for dest in stale:
            print(f"  {dest}", file=sys.stderr)
        print("\nEdit the sources under web/, then run `npm run build` there "
              "and commit the result.", file=sys.stderr)
        return 1
    if check:
        print("generated front-ends are up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
