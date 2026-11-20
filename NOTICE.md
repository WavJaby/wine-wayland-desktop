# License scope and attribution

Original wine-wayland-desktop tooling and accompanying documentation use
LGPL-2.1-or-later unless a file expressly states another license. The complete
LGPL 2.1 text is in [COPYING.LIB](COPYING.LIB); the project notice is in
[LICENSE](LICENSE).

Wine code, integration changes derived from Wine and copied Wine files retain the Wine authors' LGPL-2.1-or-later notices. Preserve the matching source, LICENSE, COPYING.LIB, AUTHORS and file-level copyright notices. A commit author in the Wine fork is not evidence of sole authorship of the code it contains.

## Third-party work in the Wine fork

The fork's integration commits import draft work from Wine merge requests (https://gitlab.winehq.org/wine/wine/-/merge_requests/N). Each author keeps their copyright; changes made here are listed in the fork's git history.

| Fork path | Source | Author | Status of the source MR | Local changes |
|---|---|---|---|---|
| `dlls/win32u/snidrv/` | !2808 SNI tray implementation | Sergei Chernyadyev | open, draft | list_mutex unlock fix on watcher owner change; hooks into `win32u` systray |
| `dlls/comdlg32/` portal backend (`portal_dbus.c`, `unixlib.*`), portal parts of `shell32` and `winecfg` | !10060 XDG desktop portal for native file dialogs | Alexander Wilms | closed, draft | policy UI, reveal-in-file-manager, event-sink and cancel handling |
| `dlls/winewayland.drv/wayland_touch.c` | !11116 winewayland touch input | Etaash Mathamsetty | open, draft | motion, lifecycle and PT_TOUCH fixes, touch-to-mouse promotion |
| `dlls/wpnapps/` `main.c`, `wpnapps.spec`, `Makefile.in`, `classes.idl`, `private.h`; `include/notificationactivationcallback.idl` | !12256 and !12433 wpnapps stub DLL | Vibhav Pant (CodeWeavers) | merged into Wine | toast bridge wiring: imports, activation, registration |

The portal sources carry the file header "Copyright 2026 Wine Project" as imported; the MR author is the person named above. The author of each imported snapshot was taken from the MR page, not from a file header, except for snidrv, wpnapps `main.c` and `wayland_touch.c`, whose headers name the author.

`dlls/wpnapps/` `toast.c`, `notify.c`, `unixlib.c`, `unixlib.h` and `xml.c` were written for this project with an AI coding assistant (Claude).
They implement WinRT toasts over org.freedesktop.Notifications and `Windows.Data.Xml.Dom.XmlDocument`.
A search of Wine merge requests and three public projects found no upstream counterpart and no copied lines; the search cannot rule out that the assistant reproduced a known shape.
This code is not intended for submission to Wine, whose contribution policy excludes AI-generated code.

Wine's AUTHORS file, LICENSE and COPYING.LIB are unchanged in the fork.

External source downloads and dependencies under src/ or tmp/, including the
Hyprland diagnostic source/candidate, remain governed by their own licenses.
Do not place them under a blanket project license. Proprietary applications,
account data, screenshots, private investigation records and signing keys are
not part of the public source release. Possession in this workspace does not
supply permission to redistribute them.

The [private-testing artifact policy](PRIVATE-TESTING.md) governs
the project's handling and intended use of its optional local signing output.
It must not be represented as a new restriction on Wine's LGPL permissions.
A locally generated certificate does not grant a vendor's identity, trademark
permission or endorsement. A notice is not proof of general legal compliance.

No release artifact has been published from this repository. Before one is,
include its source, matching license notices and dependencies, and audit the
actual package contents.
