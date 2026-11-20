# Questions and answers

## An application reports a missing or untrusted signature. Can I test local signing?

Some applications inspect signatures on Wine's replacement Windows libraries. If an application shows this problem, the optional local signing tool can help investigate it on a disposable prefix. It signs one explicitly selected Wine builtin PE file per invocation, makes a private file copy instead of modifying a shared runner, and records a backup and hashes. It does not modify the application or disable its verification logic. Generated outputs are for private compatibility testing only and must not be distributed under the project's artifact policy; see [PRIVATE-TESTING.md](PRIVATE-TESTING.md).

The observed successful method used Jsign on Linux with a newly generated self-signed certificate whose subject fields were supplied explicitly, followed by native verification and a separate certificate import into the disposable prefix's ROOT store. A startup test reached a login UI with the signed 64-bit `drive_c/windows/system32/crypt32.dll` still installed. Other PE files in that test runner were already signed: this was not an end-to-end test of an unsigned runner, proof that signing just this DLL is sufficient, or a successful account login or messaging test.

The neutral default certificate produced a valid signature and passed independent Wine chain and Authenticode policy checks after prefix-only trust import, but the tested application still rejected it with `UNTRUSTED_SIGNER`. Changing the signer subject changed the observed acceptance result; changing only the issuer in a separate two-level chain control did not. A valid signature or a trusted chain alone therefore does not guarantee application acceptance. Changing a certificate's displayed name does not make it an authentic vendor certificate. The project does not supply third-party publisher identities or promise a universally accepted identity.

## How do I choose certificate fields?

For the custom-subject method, the user supplies their own subject fields in an OpenSSL req configuration outside this repository and passes `--certificate-config`. The successful experiment supplied country (`C`), state (`ST`), locality (`L`), organisation (`O`), common name (`CN`) and a DNS subject alternative name. The following preserves that configuration's structure, with placeholders instead of identity strings; replace every placeholder with your own chosen value before use, and save it as `<certificate-config>`:

```ini
[req]
prompt = no
distinguished_name = identity
x509_extensions = extensions

[identity]
C = <two-letter-country-code>
ST = <state>
L = <locality>
O = <user-chosen-organisation>
CN = <user-chosen-common-name>

[extensions]
subjectAltName = DNS:<user-chosen-dns-name>
```

The successful configuration did not add `basicConstraints`, `keyUsage` or `extendedKeyUsage` extensions. With `--certificate-config`, the tool generates a fresh RSA-2048 key and SHA-256 self-signed certificate valid for 3650 days, copies the configuration into its private transaction directory, and records the subject and certificate hash in its manifest. It prints the subject, issuer and SHA-256 fingerprint. Jsign replaces the PE signature using a SHA-256 digest, with no timestamp server. These are local test labels, not verified publisher identities.

The experiments support a subject-dependent acceptance condition, but do not establish an exact matching rule, acceptance of arbitrary user-chosen labels, or whether the organisation, location, DNS name and every other field in the successful configuration were necessary. Omitting `--certificate-config` selects a different neutral profile (RSA-3072, 365 days, codeSigning EKU); it is not the method that demonstrated application acceptance. Older experiment commands used a bundled named profile and `--signer`; those options are absent from the current tool. Use `--certificate-config` and `--verifier` as shown below. The current custom-config interface has signing and recovery tests, but no new application-acceptance measurement was made after replacing the old profile selector.

## How do I run the tool?

Use Linux with Bash, coreutils, util-linux (`flock`), jq, OpenSSL, a Java runtime, a Jsign JAR and an osslsigncode verifier. The measured signing stack was Jsign 7.6 with JetBrains Runtime OpenJDK 25.0.3 (`JBR-25.0.3+9-508.4-lb`) and osslsigncode 2.10 loaded with OpenSSL 3.6.5 libraries. The runtime must include `java.smartcardio`; a smaller runtime without it failed. The retained verifier initially failed because its required OpenSSL 3.6 symbols were unavailable, and rebuilding that verifier against installed OpenSSL 4.1.0 failed; the working run supplied compatible OpenSSL 3.6.5 libraries only to the test process, without replacing host libraries. This records a working combination, not a claim that arbitrary versions are interchangeable or a reason to downgrade host libraries. The retained evidence identifies the verifier's OpenSSL library version more clearly than the certificate-generation CLI version.

Obtain these tools from their upstream projects or your distribution; they are not bundled. Select and initialize the final Wine runner and disposable prefix before signing, finish prefix updates, then stop every process using the chosen prefix before apply/restore. Preserve an independent backup. Keep exclusive access throughout the operation and do not launch Wine while it runs.

The tool refuses to run when it finds a process of yours that has this prefix in its `WINEPREFIX` variable, or when a relevant process environment cannot be inspected. That check is best effort: it does not see a Wine process started without `WINEPREFIX`, a process hidden in another namespace, or one started after the check. The same applies to conflict detection: it compares file hashes and then renames, so it cannot defend against another program replacing the file in between. Exclusive access to the prefix, for the whole run, is your responsibility.

Example paths below are placeholders. The source PE must be byte-identical to the target and contain Wine's builtin marker; arbitrary application executables are refused. The example uses the file measured in the signing experiment. The tool does not discover or sign all libraries an application might check, and it accepts only Wine builtin PE targets below `drive_c/windows`.

First inspect the target and read [PRIVATE-TESTING.md](PRIVATE-TESTING.md), then apply with the explicit acknowledgement and your configuration:

```bash
bash scripts/local-sign.sh inspect \
  --prefix "<prefix>" --file drive_c/windows/system32/crypt32.dll

bash scripts/local-sign.sh apply \
  --prefix "<prefix>" --file drive_c/windows/system32/crypt32.dll \
  --source "<runner>/lib/wine/x86_64-windows/crypt32.dll" \
  --jsign-jar "<jsign.jar>" --java "<java>" \
  --verifier "<osslsigncode>" \
  --certificate-config "<certificate-config>" \
  --acknowledge private-testing-v1
```

Apply verifies the staged signature with osslsigncode against the generated certificate before replacing the target. Require `Signature verification: ok`, one verified signature and successful completion; a signature check is not an application-acceptance check. Inspect again to check the installed SHA-256 against the manifest's `signed_sha256` and confirm its status is `applied`:

```bash
bash scripts/local-sign.sh inspect \
  --prefix "<prefix>" --file drive_c/windows/system32/crypt32.dll
```

The successful startup experiment then imported that transaction's certificate only into the disposable prefix's ROOT store using a separate diagnostic probe. It independently confirmed certificate presence, zero direct/provider chain errors and zero BASE/Authenticode policy errors before launching the application. The public signing tool does not perform or reverse that import and does not include the probe; the commands above reproduce the signing transaction, not the complete demonstrated trust-and-startup experiment. Keep any separate trust experiment confined to a disposable prefix and record how to undo it. After an application test, inspect again and compare the live file hash with the signed manifest: an observed runner switch triggered a Wine prefix update that silently replaced the signed DLL.

Stop the prefix again before restoring the recorded file:

```bash
bash scripts/local-sign.sh restore \
  --prefix "<prefix>" --file drive_c/windows/system32/crypt32.dll
```

An apply that fails or is interrupted before the file is renamed leaves the target unchanged and can be repeated. A later apply discards incomplete staging when the target still matches the original; apply/restore removes this target's stale replacement temporary files. If the process died after the rename but before the transaction was marked applied, the file is already signed: use restore. Restore refuses to overwrite an independently updated file. Each file has one transaction slot; a second completed apply is refused even after restore. Preserve the existing state and use a fresh disposable prefix for a new experiment. The tool is experimental, not a complete installer or certificate trust manager.

After a Wine runner change or prefix update, inspect the target again instead of assuming its signature survived. If files were replaced, finish initialization with the final runner, stop the prefix, and repeat signing and verification against a matching original source in a fresh disposable prefix while preserving the previous transaction. A new apply generates a new certificate, so any separately established prefix trust must also be established and independently checked for that new certificate. Repeat the application observation and post-launch hash comparison; do not force restore over updated files or delete transaction state to bypass the one-slot rule.

## Does it install a trusted certificate?

No. Verification against the generated certificate checks the signed file against that explicit local certificate; it does not prove system trust or application acceptance. The tool neither imports a root certificate nor changes the host or Wine trust stores. Any separate trust changes need separate review and restoration; a successful file restore does not undo them.

Earlier experiments reported aggregate `WinVerifyTrust` success even while independent chain and policy checks failed without root import. That discrepancy means aggregate success alone is insufficient evidence of trust. Progress beyond a signature check is also insufficient evidence of full startup: early program-copy tests stopped at a later installation error or exited without a usable UI; the later installed-prefix test established only the scoped login-UI result described above.

## Can I share the generated files?

No: generated signed files, private keys, certificates and prepared prefixes are for private local compatibility testing only and must not be distributed under the project's artifact policy. Do not upload the prefix's `.wine-wayland-signing` directory or include it in bug reports. See [PRIVATE-TESTING.md](PRIVATE-TESTING.md). This policy does not override Wine's LGPL rights. Do not publish private keys or describe self-signed test files as vendor-issued. A disclaimer alone does not establish legal compliance.
