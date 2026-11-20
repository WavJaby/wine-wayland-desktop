# License scope and attribution

Original wine-wayland-desktop tooling and accompanying documentation use
LGPL-2.1-or-later unless a file expressly states another license. The complete
LGPL 2.1 text is in [COPYING.LIB](COPYING.LIB); the project notice is in
[LICENSE](LICENSE).

Wine code, integration changes derived from Wine and copied Wine files retain
the Wine authors' LGPL-2.1-or-later notices. Preserve the matching source,
LICENSE, COPYING.LIB, AUTHORS and file-level copyright notices. Imported
proposals include Wine MRs 2808,10060 and11116; local integration commit authors
are not evidence of sole authorship. See
the matching Wine source history and file-level notices.

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

The historical source exporter currently includes only a pinned Wine snapshot
and an explicit documentation allowlist. Before adding tooling to a public
artifact, include its source, matching license notices and dependencies, and
audit the actual package contents. This license pass does not publish an
artifact or update a historical archive.
