# Questions and answers

## An application reports a missing or untrusted signature. Can I test local signing?

Some applications inspect signatures on Wine's replacement Windows libraries.
The optional local signing tool can help investigate that compatibility issue
on a disposable prefix. It signs only Wine builtin PE files, makes a private
file copy instead of modifying a shared runner and records a backup and hashes.
It does not modify the application or disable its verification logic.

A valid signature is not a guarantee of acceptance. An application can require
a particular trusted publisher or apply additional rules that a self-signed
certificate does not satisfy. Changing a certificate's displayed name does not
make it an authentic vendor certificate. The project does not supply suggested
third-party publisher identities or promise a universally accepted identity.

## How do I choose certificate fields?

Without an extra option the tool uses a neutral local-test identity. To supply
your own fields, create an OpenSSL req configuration outside this repository
and pass --certificate-config /absolute/path/to/local-test.cnf. For example:

```ini
[req]
prompt = no
distinguished_name = identity
x509_extensions = extensions

[identity]
CN = My Private Compatibility Test
O = My Local Test Environment

[extensions]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature
extendedKeyUsage = codeSigning
```

Replace the example labels with your own local-test labels. Optional country,
state, locality and subject alternative names follow OpenSSL's configuration
syntax. The tool creates a fresh local key and self-signed certificate, keeps
the supplied configuration in its private transaction directory and reports
the resulting subject and SHA-256 certificate fingerprint. It does not provide
a trusted commercial signing identity.

## How do I run the tool?

Use Linux with Bash, coreutils, util-linux (flock), jq, OpenSSL, a Java runtime
supported by your Jsign version, a Jsign JAR and an osslsigncode verifier.
Obtain those tools from their upstream projects or your distribution; they are
not bundled. Stop every process using the chosen prefix before apply/restore.
Use a disposable prefix first and preserve an independent backup.

Example paths below are placeholders. The original PE must be byte-identical
to the target and contain Wine's builtin marker. The tool deliberately refuses
arbitrary application executables.

```bash
bash scripts/local-sign.sh inspect \
  --prefix /path/to/test-prefix --file drive_c/windows/system32/example.dll

bash scripts/local-sign.sh apply \
  --prefix /path/to/test-prefix --file drive_c/windows/system32/example.dll \
  --source /path/to/runner/lib/wine/x86_64-windows/example.dll \
  --jsign-jar /path/to/jsign.jar --java /path/to/java \
  --verifier /path/to/osslsigncode \
  --certificate-config /path/to/local-test.cnf \
  --acknowledge private-testing-v1

bash scripts/local-sign.sh restore \
  --prefix /path/to/test-prefix --file drive_c/windows/system32/example.dll
```

Omit --certificate-config to test the neutral default. An apply that fails or
is interrupted before the file is replaced leaves the target unchanged and can
be repeated; staging left by a killed process is removed by the next apply. Restore refuses to
overwrite an independently updated file. Each file has one transaction slot;
a second apply is refused even after restore. Preserve the existing state and
use a fresh disposable prefix for a new experiment. The tool is experimental,
not a complete installer or certificate trust manager.

## Does it install a trusted certificate?

No. Verification against the generated certificate checks the signed file
against that explicit local certificate; it does not prove system trust or
application acceptance. The tool neither imports a root certificate nor changes
the host trust store. Any separate trust changes need separate review and
restoration; a successful file restore does not undo them.

## Can I share the generated files?

The project's intended use is private local compatibility testing, and the
project does not distribute signed output, keys or prepared prefixes. See
[PRIVATE-TESTING.md](PRIVATE-TESTING.md). This policy does not override Wine's
LGPL rights. Do not publish private keys or describe self-signed test files as
vendor-issued. A disclaimer alone does not establish legal compliance.
