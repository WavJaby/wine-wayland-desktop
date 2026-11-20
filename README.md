# wine-wayland-desktop

A Wine integration project for Windows desktop applications through Wine's
native Wayland driver, without an X11/XWayland display path. Desktop work
includes window sizing, touch, monitor DPI, tray integration, notifications,
clipboard and portal dialogs. Compatibility varies by application and desktop;
this is a development preview, not a stable compatibility guarantee.

This repository holds everything except the Wine source itself, which lives
in a separate Wine fork. [wine.lock](wine.lock) pins the exact fork commit and
tree; `scripts/check-wine-source.sh WINE_CHECKOUT` verifies a checkout against
it. A public runner download and complete installation procedure are not yet
available from this checkout.

- `broker/`: launch broker that lets separate sandboxed launches of one
  prefix share a single wineserver (C source, protocol tests, integration gates)
- `scripts/`: local signing tool and the Wine source check
- `tests/`: tests for both
- `probes/`: small diagnostic programs for window, input and tray behavior

The distribution design separates an unsigned general Wine runner from an
optional Linux-only local signing tool. See [FAQ.md](FAQ.md) for that tool's
purpose, prerequisites, usage and limitations, and
[PRIVATE-TESTING.md](PRIVATE-TESTING.md) for the generated-output notice.

Private investigation notes, application data, builds, keys and test prefixes
are excluded from project distribution. Local docs/ notes are not public
installation instructions. Historical source-preview archives under dist/
are not current validated binary releases.

Original project code and documentation use LGPL-2.1-or-later unless stated
otherwise. Third-party components retain their own licenses and authors.
See [LICENSE](LICENSE), [COPYING.LIB](COPYING.LIB) and [NOTICE.md](NOTICE.md).
