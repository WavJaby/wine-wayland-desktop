# Private local compatibility testing

The optional local signing tool creates experimental output on your machine.
The project intends it only for your own private compatibility testing, and
does not distribute generated signed files, private keys, certificates or
prepared application prefixes. The intended distribution is an unsigned
general runner plus this separate tool; no runner is published yet.

A locally generated certificate is self-signed. Its subject fields are labels
chosen by its creator, not verified evidence of a publisher's identity,
authorization or endorsement. Entering an organization's name does not create
a genuine signature from that organization. Do not present the result as one.

Signature verification, certificate trust and application acceptance are
separate checks. This tool does not guarantee application acceptance or legal
compliance. Use it only on Wine builtin PE files you are entitled to modify.

Keys remain in the selected prefix's .wine-wayland-signing directory with
private permissions. Do not upload that directory or include it in bug reports.
The tool does not import certificates into the host or Wine trust stores.
Restore reverses the recorded file change, not any manual trust-store changes.

This notice describes the project's distribution policy and intended testing
use. It does not add restrictions to rights granted by Wine's LGPL or any
other applicable license. Preserve applicable source and license obligations.

Apply requires explicit acknowledgement: --acknowledge private-testing-v1.
