# Batch C: bounded HTTPS transport fixtures — October 12, 2026

## Outcome and scope

Implemented `transport::SwapHttpsClient`, exercised only against disposable
loopback TLS servers. Ten integration tests pass under ASan/UBSan and nine
mutations fail named assertions. This advances item 10's transport mechanics;
native Linux ARM qualification, release dependency pinning, real authentication,
swap request fields, signer integration and settlement evidence remain open.

The client is generic byte transport with an explicit method/path allowlist. It
does not reuse the old MCP transport's envelope, infer an execute endpoint, or
translate a fixed-base limit into a swap. Fixture POSTs use synthetic JSON only.
No venue, RPC, account session, real token, signer, host operation, deployment or
commit was used. No shared build/tracker file changed in this batch.

Files: `swap_https.hpp`, `v2_swap_https.cpp`, `v2_swap_https_probe.cpp`,
`test_swap_https.py`, `mutation_swap_https.py` and this document. The source
requires `VELOCITA_SWAP_HTTPS_TEST_ONLY`; a compile without that explicit opt-in
fails. Parent integration must keep the target out of release/deployment graphs.
The test probe restricts actual origins to localhost/127.0.0.1. The underlying
client permits a configured HTTPS DNS/IPv4 origin for future reviewed integration;
that capability is not permission to contact a venue.

## Contract and ownership

`SwapHttpsConfig` owns origin, optional CA-bundle path, total timeout, response
limit, and at most eight exact method/path pairs. `exchange` accepts borrowed
request path, bearer and body bytes for the duration of its synchronous call.
It returns a fixed-capacity owned response buffer. A caller may retain the
result after another request and after the client is destroyed.

The method enum admits GET and POST. Paths must match an explicit allowlist
entry, be relative absolute-path references beginning with one slash, and contain
only ASCII letters/digits, slash, underscore and hyphen. Queries, fragments,
percent encoding, dot segments, repeated/trailing slashes, URL userinfo and
arbitrary schemes are refused. This deliberately narrow grammar is not a general
URL client. IPv6 literals are outside this first profile. A configured hostname
may use an explicit port in 1–65535.

GET bodies and empty POST bodies are refused. Body bytes are opaque; transport
does not certify that a payload is valid JSON or a supported venue instruction.
Bearer input is bounded and excludes whitespace/control characters to prevent
header injection. The caller's allowlist and request encoder remain trusted
configuration that future gateway integration must bind to committed intent.

| Bound | Value |
|---|---:|
| Request body | 16,384 bytes |
| Response body | Configurable 1–65,536 bytes; fixed owning 65,536-byte array |
| Aggregate response headers | 8,192 bytes |
| Bearer string | At most 8,192 visible ASCII bytes |
| Path | 2–256 bytes |
| Origin | At most 280 bytes, validated host/port |
| CA path | At most 4,096 bytes, no NUL |
| Whole-request and connection timeout | 1–30,000 ms |
| Allowed routes | 1–8 distinct exact method/path pairs |

Configuration/request string allocation and libcurl work stay outside the Raft
reducer. The fixed body bound does not claim to bound libcurl/TLS allocator use.
Use the client from the gateway's bounded worker; do not mutate borrowed request
storage during the call. There is no background operation or callback retained
after return. Response callbacks perform checked size arithmetic and do not throw.

## Outcomes do not settle trades

| Exposure | Meaning |
|---|---|
| `not_attempted` | Rejected/configuration or library setup failed before `curl_easy_perform`; this call made no transfer attempt |
| `possibly_sent` | Transfer was attempted but no complete accepted HTTP response was obtained; persist/reconcile the same operation |
| `response_received` | A complete HTTP response was received; interpret it separately under the venue contract |

Once a transfer begins, timeout, TLS failure, oversized output and transport
failure conservatively retain `possibly_sent`. TLS refusal may in fact occur
before any HTTP bytes, but this API deliberately does not offer that finer
non-submission proof. Failure never means `failed_without_charge` or releases a
kernel reservation. A completed HTTP 401/429/500 is a response, not a successful
trade or authoritative failure-without-fee. A lost acknowledgement after the
fixture records the POST is ambiguous, and is never retried.

Redirects are reported separately with the received status and no body. All
incomplete/refused response bodies are zeroed before return, preventing a partial
JSON body from being mistaken for complete evidence. Complete error bodies remain
owned data for a later strict venue parser. The transport writes no diagnostics,
headers, bodies or tokens to logs; the probe prints only status/exposure/counts
and separately requested dependency metadata.

## TLS and request policy

Each exchange creates a fresh libcurl handle and one application-level perform
call. It forces HTTP/1.1, fresh connections and connection closure; no shared
connection cache, HTTP/2 stream retry, redirect following, proxy, netrc lookup,
authentication challenge negotiation, automatic Expect header or application
retry is enabled. The explicit User-Agent is `velocita-swap-fixture/1`, matching
the current test-only component. Future production naming must remain truthful.
Explicit localhost ports are resolved to 127.0.0.1 while certificate hostname
verification still uses localhost.

Peer verification is 1, hostname verification 2, and minimum TLS is 1.2. Only
HTTPS protocol handlers are allowed. Set-option failures stop before perform.
Total and connect timeouts are both applied; the runtime must advertise TLS and
asynchronous DNS, and be at least libcurl 7.85 for the required protocol-string
options. That version floor is an API/feature requirement, **not** a claim that
every version above it is security-qualified.

The [libcurl peer-verification reference](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html)
separates certificate-chain trust from hostname verification. Its
[total-timeout reference](https://curl.se/libcurl/c/CURLOPT_TIMEOUT_MS.html)
defines the whole-transfer deadline. [Fresh-connect](https://curl.se/libcurl/c/CURLOPT_FRESH_CONNECT.html)
and [forbid-reuse](https://curl.se/libcurl/c/CURLOPT_FORBID_REUSE.html) describe
the deliberate connection-reuse restriction and its performance cost. These
settings and fixture request counts establish the tested no-retry behavior;
future dependency/backend changes must rerun the failure tests rather than
assuming identical library behavior.

TLS peer trust currently uses the supplied fixture CA, or the configured
system trust store if no bundle is supplied. No certificate/public-key pin,
revocation guarantee, custom cipher allowlist or trusted production CA artifact
has been established by this batch. Native policy and exact dependency artifacts
must be reviewed before making the client production-capable.

## Evidence and test chronology

Scratch: `~/.cache/velocita-build/abc-transport-20261012/`.

The test/header/probe were created with a refusing transport stub. An initial
unused-field compiler error and a sandbox loopback-bind error were not counted
as RED. After fixing the fixture environment, the compiled stub failed the named
`trusted_get_failed` assertion (`red.log`). Only then was the transport implemented.
Later TLS-version and configuration cases are not claimed to have existed in
that original RED run.

`green.log` records ten passing ASan/UBSan tests covering:

- Successful GET/POST, exact synthetic bytes and truthful User-Agent; owned
  result lifetime across a second request and client destruction.
- Wrong hostname, untrusted CA, expired certificate and obsolete TLS 1.1.
- POST redirect refusal with exactly one received request.
- Body-read timeout and lost acknowledgement after a recorded POST, each with
  ambiguous exposure and exactly one request.
- Exact body capacity, one-byte excess and oversized headers.
- HTTP 401, 429 and 500 without authentication negotiation or retry.
- Invalid origin, path/allowlist, method/body combinations, injected bearer,
  request size, timeout and response-limit bounds before network use.
- Actual linked runtime fingerprint availability.

`mutations-1791829388842127000/results.json` records nine named-assertion kills:
hostname verification, peer verification, redirect policy, body capacity, header
capacity, total deadline, ambiguity classification, added retry and spoofed
User-Agent. A passing baseline runs first. Compile errors, timeouts, fixture
errors and unrelated failures are not counted as kills. Mutants are copied into
unique scratch directories; repository implementation bytes are not rewritten.

Reproduce the Mac fixture run from the repository root:

```zsh
S="$HOME/.cache/velocita-build/abc-transport-20261012"
mkdir -p "$S"
c++ -std=c++20 -Wall -Wextra -Werror -pedantic \
  -DVELOCITA_SWAP_HTTPS_TEST_ONLY=1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
  src/v2_swap_https.cpp tests/v2_swap_https_probe.cpp -lcurl -o "$S/probe"
VELOCITA_SWAP_HTTPS_PROBE="$S/probe" python3 -B tests/test_swap_https.py
python3 -B tests/mutation_swap_https.py
"$S/probe" --fingerprint
```

The local sandbox requires permission to bind loopback sockets; that exception
does not permit venue requests. The test source uses only synthetic tokens and
disposable locally generated certificate keys.

## Actual build fingerprint and what remains unpinned

`build-manifest.json` contains source/test hashes, the probe hash, exact compile
command, link dependencies, compiler, OS/SDK and runtime metadata. Observed:

| Field | Actual local value |
|---|---|
| Machine / executable | macOS arm64 / Mach-O arm64 |
| OS build / SDK | macOS 26.6.2, 25G83 / SDK 26.5 |
| Compiler | Apple clang 21.0.0, clang-2100.1.1.101 |
| libcurl headers / runtime | 8.7.1 / 8.7.1 |
| libcurl-reported TLS backend | `(SecureTransport) LibreSSL/3.3.6` |
| libcurl host metadata | `x86_64-apple-darwin25.0`; this is library metadata, not the observed probe architecture |
| Fixture server TLS | Python OpenSSL 3.6.3, 9 Jun 2026 |
| Linked curl install name | `/usr/lib/libcurl.4.dylib` |

The standalone runtime dylib is unavailable as a normal file because the system
image resolves through macOS's dyld shared cache. Therefore no runtime-library
file hash is claimed. The SDK link stub is not that runtime binary. Recorded
SHA-256 values are:

- SDK curlver header: `454e77732340eeb1119d47f501fa448a3577ea613d9e98d81b36d5bb72c242df`.
- SDK libcurl stub: `1b6d181ac7c9f13cbd270d8b1c81738ebefa45308baf53f82912b5f127006116`.
- Sanitized probe: `bba50c11fe16e9e0e4829ca4e82882948506f52908a8676b9c1f8945ea36373a`.

These identify local evidence. They do not close release pinning, attest a
vulnerability-free dependency, or qualify Linux/OpenSSL behavior.

## Native Linux ARM verification preparation — not executed

Use a reviewed committed checkout and pinned build image/package inputs. Do not
build a release from this dirty working tree or run these against live services.
The eventual target must remain a standalone test target. After native build
approval, the fixture-only command skeleton is:

```sh
uname -a
git rev-parse HEAD
git status --short
c++ --version
pkg-config --modversion libcurl
curl-config --version
curl-config --ssl-backends
openssl version -a
```

Require the intended clean committed source and expected architecture before
continuing. Build using structured arguments so dependency flags are not lost:

```python
import os, pathlib, shlex, subprocess
root = pathlib.Path.cwd()
work = pathlib.Path.home() / '.cache/velocita-build/abc-transport-native'
work.mkdir(parents=True, exist_ok=True)
cflags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'libcurl'], text=True))
libs = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'libcurl'], text=True))
binary = work / 'probe'
command = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic',
           '-DVELOCITA_SWAP_HTTPS_TEST_ONLY=1', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-Iinclude', *cflags,
           'src/v2_swap_https.cpp', 'tests/v2_swap_https_probe.cpp', *libs, '-o', str(binary)]
subprocess.run(command, check=True)
subprocess.run([str(binary), '--fingerprint'], check=True)
env = os.environ.copy()
env['VELOCITA_SWAP_HTTPS_PROBE'] = str(binary)
subprocess.run(['python3', '-B', 'tests/test_swap_https.py'], env=env, check=True)
subprocess.run(['file', str(binary)], check=True)
subprocess.run(['ldd', str(binary)], check=True)
```

Retain a native manifest containing commit and clean-state proof, image digest,
compiler/toolchain, architecture, compile/link flags, curl header/runtime/backend
metadata, resolved shared-library paths and SHA-256 hashes, package versions and
package-artifact/image provenance, CA bundle hash, executable hash, fixture-server
TLS version and full test logs. Hash the actual resolved libcurl/TLS files, not
only symlinks or SDK stubs. Document any dynamically loaded TLS backend explicitly.
Review the supported security patch level and backend-specific trust behavior.

Run mutations with the same compiler/header/link selection; the current runner
uses default `c++` and `-lcurl`, so first prove that those resolve to the pinned
native dependencies or add a reviewed explicit build-input option. Do not treat
a different backend's mutation result as evidence for the release binary.

Only after native evidence, dependency review and production build guards should
gateway integration adopt this transport. Durable before-send records, request
identity, process restart, signer authority, live endpoint contracts and terminal
reconciliation require their own drills; this client intentionally implements
none of those higher-level permissions or economic decisions.
