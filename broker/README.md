# Wine launch broker

Development implementation for Linux. Build with `make`; install the resulting
`wine-dispatch` into a self-contained runner's `bin/`. Preserve its original
`wine` and `wineserver` as `wine-real` and `wineserver-real`, then make `wine`
and `wineserver` symlinks to `wine-dispatch`. Wine tool aliases must still reach
`wine` with their original basename. Do not replace binaries while prefix
applications are running. The Python prototype is retained for comparison only.

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
- `bash gate.sh cross`, `concurrent`, `signals`, `bottles`: real Wine in the
  existing **Wine-Wayland-Test** bottle, across fresh Flatpak instances
- `python3 sandbox-gate.py`: temporarily enables that test bottle's actual
  dedicated sandbox option, launches its registered NsProbe twice using
  Bottles CLI, checks one namespace, and restores the exact config in `finally`

The last two gates are local development scripts with this workspace's test
bottle/runner paths. They must not be pointed at an active user-data prefix.
No test authorizes modifying the production LINE runner.
