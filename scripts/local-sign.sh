#!/usr/bin/env bash
# Experimental single-file Wine PE transaction. SPDX-License-Identifier: LGPL-2.1-or-later
set -euo pipefail
export LC_ALL=C
umask 077
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
notice=$root/PRIVATE-TESTING.md
operation=inspect prefix= relative= source= signer=osslsigncode java=java jsign_jar= profile=neutral certificate_config= acknowledgement=
fail() { echo "$*" >&2; exit 1; }
usage() {
    echo 'Usage: local-sign.sh [inspect|apply|restore] --prefix DIR --file RELATIVE_PE'
    echo '  apply: --source ORIGINAL_WINE_PE --jsign-jar FILE [--java EXECUTABLE]'
    echo '         [--verifier OSSLSIGNCODE_VERIFIER] [--certificate-config FILE]'
    echo '         --acknowledge private-testing-v1'
    echo '  Certificate fields come from your OpenSSL req config; default: neutral local identity'
    echo '  Read PRIVATE-TESTING.md and FAQ.md before applying; stop the prefix first'
}
if [[ ${1:-} =~ ^(inspect|apply|restore)$ ]]; then operation=$1; shift; fi
while (($#)); do
    case $1 in
        --prefix|--file|--source|--verifier|--jsign-jar|--java|--certificate-config|--acknowledge)
            (($# >= 2)) || fail "Missing value for $1"
            case $1 in
                --prefix) prefix=$2;; --file) relative=$2;; --source) source=$2;;
                --verifier) signer=$2;; --jsign-jar) jsign_jar=$2;; --java) java=$2;; --certificate-config) certificate_config=$2;; --acknowledge) acknowledgement=$2;;
            esac
            shift 2;;
        -h|--help) usage; exit 0;;
        *) fail "Unknown argument: $1";;
    esac
done
if [[ -n $certificate_config ]]; then
    certificate_config=$(realpath -e -- "$certificate_config")
    [[ -f $certificate_config && -r $certificate_config ]] || fail 'Certificate config must be a readable file'
    profile=custom
fi
for tool in jq realpath sha256sum flock openssl od sync; do command -v "$tool" >/dev/null || fail "Missing tool: $tool"; done
[[ -n $prefix && -n $relative ]] || { usage; exit 1; }
prefix=$(realpath -e -- "$prefix")
[[ $relative != /* && /$relative/ != */../* && /$relative/ != */./* && $relative != *$'\n'* ]] || fail 'Unsafe relative path'
[[ $relative == drive_c/windows/* && $relative != */ ]] || fail 'Only Wine PE files below drive_c/windows are allowed'
target=$prefix/$relative
parent=$(realpath -e -- "$(dirname -- "$target")")
[[ $parent == "$prefix/drive_c/windows" || $parent == "$prefix/drive_c/windows/"* ]] || fail 'Parent escapes Windows directory'
[[ -f $target ]] || fail 'Target is not a regular file or file symlink'
hash() { sha256sum -- "$1" | cut -d ' ' -f1; }
key=$(printf '%s' "$relative" | sha256sum | cut -d ' ' -f1)
tag=${key:0:16} # names this target's temporary files, so stale ones can be removed without touching another target's
state=$prefix/.wine-wayland-signing/$key
manifest=$state/manifest.json
if [[ $operation == inspect ]]; then
    saved=null
    [[ ! -e $manifest ]] || saved=$(cat -- "$manifest")
    jq -n --arg file "$target" --arg resolved "$(realpath -e -- "$target")" \
        --arg sha256 "$(hash "$target")" --argjson manifest "$saved" \
        '{file:$file,resolved:$resolved,sha256:$sha256,trust_changes:[],manifest:$manifest}'
    exit 0
fi
# Notice must be acknowledged before generating keys or state.
if [[ $operation == apply ]]; then
    cat -- "$notice"
    [[ $acknowledgement == private-testing-v1 ]] || fail 'Read notice and pass --acknowledge private-testing-v1'
fi
[[ ! -L $prefix/.wine-wayland-signing && ! -L $state ]] || fail 'Symlink state directory refused'
mkdir -p -- "$state"
[[ $(stat -c %u -- "$state") == "$UID" ]] || fail 'State owner mismatch'
[[ $(stat -c %a -- "$state") == 700 ]] || fail 'State must have mode 700'
[[ ! -L $state/lock ]] || fail 'Symlink lock refused'
exec {lock_fd}>"$state/lock"
flock -x "$lock_fd"
offline() {
    local proc field env_fd
    for proc in /proc/[0-9]*; do
        [[ -d $proc && -O $proc ]] || continue
        # The privileged user service manager and its PAM helper cannot be
        # Wine clients; both can deny environ reads even to their own uid.
        case $(cat "$proc/comm" 2>/dev/null || true) in
            systemd|'(sd-pam)') continue;;
        esac
        # A zombie or exiting process has no environment to read and cannot be a Wine client.
        case $(awk '/^State:/{print $2}' "$proc/status" 2>/dev/null || true) in
            Z|X|'') continue;;
        esac
        if ! { exec {env_fd}<"$proc/environ"; } 2>/dev/null; then
            [[ ! -d $proc ]] && continue
            fail "Cannot read process environment: $proc ($(cat "$proc/comm" 2>/dev/null || true))"
        fi
        while IFS= read -r -d '' field; do
            if [[ $field == WINEPREFIX=* ]]; then
                [[ $(realpath -m -- "${field#WINEPREFIX=}") != "$prefix" ]] || fail "Prefix in use: $proc"
            fi
        done <&"$env_fd"
        exec {env_fd}<&-
    done
}
temporary= staged=
# apply that fails or is interrupted before the target is renamed leaves it untouched: drop the half-built transaction so the slot stays usable. After the rename only the manifest drives restore.
rollback_staging() {
    [[ -n $staged && $(hash "$target" 2>/dev/null) == "$original" ]] || return 0
    rm -f -- "$state"/{original,signed,key.pem,certificate.pem,certificate.cnf,plan.json,manifest.json}
}
cleanup() {
    local rc=$?
    [[ -z $temporary ]] || rm -f -- "$temporary"
    rollback_staging
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
# Stage in the destination directory, then rename and flush the filesystem.
replace_file() {
    local input=$1 mode=$2
    temporary=$(mktemp -- "$parent/.local-sign-$tag-XXXXXXXX")
    cat -- "$input" > "$temporary"
    chmod "$mode" "$temporary"
    sync -f "$temporary"
    mv -Tf -- "$temporary" "$target"
    temporary=
    sync -f "$parent"
}
write_manifest() {
    local status=$1
    temporary=$(mktemp -- "$state/.manifest-XXXXXXXX")
    jq --arg status "$status" '.status=$status' "$state/plan.json" > "$temporary"
    sync -f "$temporary"
    mv -Tf -- "$temporary" "$manifest"
    temporary=
    sync -f "$state"
}
offline
# Holding this target's lock: a temporary left by a killed run (SIGKILL, power loss) can only be this target's.
rm -f -- "$parent"/.local-sign-"$tag"-*
if [[ $operation == restore ]]; then
    [[ -f $manifest && ! -L $manifest ]] || fail 'Missing or unsafe manifest'
    [[ $(jq -r .file "$manifest") == "$relative" ]] || fail 'Manifest target mismatch'
    original=$(jq -r .original_sha256 "$manifest")
    signed=$(jq -r .signed_sha256 "$manifest")
    current=$(hash "$target")
    [[ $current == "$original" || $current == "$signed" ]] || fail 'Restore conflict: current file changed'
    [[ $(hash "$state/original") == "$original" ]] || fail 'Backup integrity failure'
    link=$(jq -r '.symlink // empty' "$manifest")
    if [[ -n $link ]]; then
        temporary=$(mktemp -- "$parent/.local-sign-$tag-XXXXXXXX")
        rm -- "$temporary"
        ln -s -- "$link" "$temporary"
        [[ $(hash "$temporary") == "$original" ]] || fail 'Original symlink destination changed'
        mv -Tf -- "$temporary" "$target"
        temporary=
        sync -f "$parent"
    else
        replace_file "$state/original" "$(jq -r .mode "$manifest")"
    fi
    cp -- "$manifest" "$state/plan.json"
    write_manifest restored
    exit 0
fi
# The manifest is written ("prepared") before the target is renamed. No manifest, or "prepared" with the target still equal to the recorded original, means the target was never touched:
# staging left by a killed apply (SIGKILL, power loss) is safe to drop and the slot is reusable. Any other state belongs to inspect/restore.
if [[ -e $manifest ]]; then
    [[ ! -L $manifest && $(jq -r .status "$manifest") == prepared && $(hash "$target") == "$(jq -r .original_sha256 "$manifest")" ]] ||
        fail 'Existing transaction: inspect/restore; do not reuse'
fi
rm -f -- "$state"/{original,signed,key.pem,certificate.pem,certificate.cnf,plan.json,manifest.json} "$state"/.manifest-*
[[ -n $source && -f $source && $(hash "$source") == "$(hash "$target")" ]] || fail 'Supply an identical original Wine runner PE with --source'
[[ $(od -An -tx1 -N2 -- "$target" | tr -d ' \n') == 4d5a ]] || fail 'Not a PE file'
# Wine writes this exact marker immediately after the 64-byte DOS header.
[[ $(dd if="$target" bs=1 skip=64 count=17 status=none | od -An -tx1 | tr -d ' \n') == 57696e65206275696c74696e20444c4c00 ]] || fail 'Not a Wine builtin PE'
[[ -f $jsign_jar ]] || fail 'Supply --jsign-jar /path/to/jsign.jar'
original=$(hash "$target")
staged=1
mode=$(stat -Lc %a -- "$target")
link=null
[[ ! -L $target ]] || link=$(readlink -- "$target" | jq -Rs 'rtrimstr("\n")')
cp --dereference -- "$target" "$state/original"
chmod 600 "$state/original"
if [[ $profile == custom ]]; then
    cp -- "$certificate_config" "$state/certificate.cnf"
    openssl req -x509 -new -nodes -sha256 -utf8 -days 3650 -newkey rsa:2048 \
        -keyout "$state/key.pem" -out "$state/certificate.pem" \
        -batch -config "$state/certificate.cnf"
else
    openssl req -x509 -newkey rsa:3072 -nodes -days 365 -sha256 \
        -subj '/CN=Wine Wayland Private Local Test' \
        -addext 'extendedKeyUsage=codeSigning' \
        -keyout "$state/key.pem" -out "$state/certificate.pem"
fi
cp -- "$state/original" "$state/signed"
"$java" -jar "$jsign_jar" --replace --certfile "$state/certificate.pem" \
    --keyfile "$state/key.pem" --alg SHA-256 \
    --name 'PRIVATE LOCAL TEST - NOT A VENDOR SIGNATURE' "$state/signed"
"$signer" verify -CAfile "$state/certificate.pem" -in "$state/signed"
openssl x509 -in "$state/certificate.pem" -noout -subject -issuer -fingerprint -sha256
jq -n --arg file "$relative" --arg original "$original" --arg signed "$(hash "$state/signed")" \
    --arg mode "$mode" --argjson link "$link" --arg cert "$(hash "$state/certificate.pem")" \
    --arg subject "$(openssl x509 -in "$state/certificate.pem" -noout -subject -nameopt RFC2253)" \
    --arg profile "$profile" --arg notice "$(hash "$notice")" \
    '{file:$file,original_sha256:$original,signed_sha256:$signed,mode:$mode,symlink:$link,
      certificate_profile:$profile,certificate_subject:$subject,signing_tool:"Jsign",certificate_sha256:$cert,notice:"private-testing-v1",notice_sha256:$notice,trust_changes:[]}' > "$state/plan.json"
write_manifest prepared
offline
[[ $(hash "$target") == "$original" ]] || fail 'Apply conflict: target changed during staging'
replace_file "$state/signed" "$mode"
staged=
write_manifest applied
