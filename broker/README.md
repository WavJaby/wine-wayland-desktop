# Wine launch broker

Development implementation for Linux. Build with `make`; install the resulting
`wine-dispatch` into a self-contained runner's `bin/`. Preserve its original
`wine` and `wineserver` as `wine-real` and `wineserver-real`, then make `wine`
and `wineserver` symlinks to `wine-dispatch`. Wine tool aliases must still reach
`wine` with their original basename. Do not replace binaries while prefix
applications are running.

The first caller elects a detached owner in its PID namespace. All invocations,
including direct wineserver operations, use authenticated Unix IPC to execute
in that owner's namespace. A forked worker per request transports standard FDs,
argv, environment, cwd, exit status and INT/TERM/HUP. Closing a proxy does not
kill its child. No interpreter, JSON library, threads or elevated privileges
are needed. Broker logs contain PIDs/status, not arguments or environment values.

Election and socket files live in the prefix's `.wine-wayland-broker/` (0700),
because dedicated Flatpak sandboxes do not share `/tmp`. Socket paths use an
opened directory FD, avoiding Unix socket pathname length limits. A fixed
versioned endpoint and wire magic keep this protocol separate from the prototype.
The broker exits after 15 seconds with no active request or wineserver lock.
A subsequent caller recovers a stale socket under the election lock.

Peers must have the same uid, runner executable directory and sandbox profile.
The profile is the complete `/.flatpak-info` minus instance ID/path; host
launches have a distinct profile. Different permissions, runtime versions or
exposed paths are rejected rather than executed in another security context.
This deliberately conservative equality check may reject redundant exposure
changes. Close all prefix applications before changing sandbox settings or
runner versions. A live unbrokered server in the caller's `/tmp` is refused.
A prefix already running outside this broker in a different private `/tmp`
cannot be detected by that lock check; never mix launch mechanisms.

This is a development gate, not a portable binary release. Build against the
release runtime baseline before distribution. Tests on this host use Linux
`close_range`, `/proc`, `SO_PEERCRED`, SCM_RIGHTS and the Bottles GNOME runtime.
The broker does not provide a security boundary between mutually untrusted
processes of the same uid. It retains the elected owner's Flatpak lifecycle;
GUI close/stop policy still belongs to Bottles and requires explicit testing.

## Verification

- `python3 test-dispatch.py`: disposable fake runner; argv (including Unicode,
  shell metacharacters and empty values), env, cwd, stdin/stdout, exit 7,
  concurrent children, INT/TERM transport and idle exit
- `bash gate.sh cross`, `concurrent`, `signals`, `bottles`: real Wine in a
  disposable Bottles test bottle, across fresh Flatpak instances
- `python3 sandbox-gate.py`: temporarily enables that test bottle's actual
  dedicated sandbox option, launches its registered NsProbe program twice using
  Bottles CLI, checks one namespace, and puts the config back unless it changed meanwhile

The last two gates are development scripts for a Bottles + Flatpak setup. They
refuse to run unless `BROKER_GATE_BOTTLE` names the test bottle (`gate.sh` also
needs `BROKER_GATE_RUNNER`, the brokered runner's directory name, and
`BROKER_GATE_STOCK_RUNNER` for `control`). They run Wine, create files and kill
the wineserver in that bottle: never name a bottle in daily use. The test
programs `nsprobe.exe` and `hungprobe.exe` are Windows executables you build
yourself from `nsprobe.c` and `hungprobe.c` (for example with
`x86_64-w64-mingw32-gcc`); put them in the bottle's `C:\` and register
`nsprobe.exe` as a Bottles program named NsProbe. `second-window.sh` also changes the bottle's graphics driver key and
takes a whole-desktop screenshot only with `SW_SCREENSHOT=1`.
