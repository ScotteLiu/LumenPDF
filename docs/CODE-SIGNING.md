# Code signing

LumenPDF's Windows builds are **not signed** unless a certificate is configured.
This page explains what that costs, and how to sign your own builds.

## What an unsigned build does

Windows SmartScreen shows *"Windows protected your PC — Microsoft Defender
SmartScreen prevented an unrecognised app from starting"* on first run. Getting
past it takes two clicks through *More info → Run anyway*, and most people close
the dialog instead.

Nothing about the application is different. The warning is about the publisher
being unidentified, not about the code.

## Why it is not just switched on

A code-signing certificate is issued to an **identified legal entity** after
that entity proves who it is — company registration documents, or for an
individual, government ID and a verifiable address. It cannot be generated,
downloaded, or shared, and a self-signed certificate does not help: Windows
trusts the certificate authority, not the signature.

So this is a decision for whoever publishes the build, and it costs money:

| | Roughly | Effect |
|---|---|---|
| **OV** (organisation validated) | $200–400 / year | SmartScreen reputation builds up over downloads. Early installs still warn. |
| **EV** (extended validation) | $400–700 / year, plus a hardware token or cloud HSM | Immediate SmartScreen trust, no reputation period. |
| **Azure Trusted Signing** | ~$10 / month | Microsoft-run signing service. Individuals need 3 years of verifiable history. |

Since June 2023 every new certificate's private key must live on FIPS 140-2
hardware, so an OV or EV certificate arrives as a USB token or as access to a
cloud HSM — you cannot simply hold a `.pfx` file for a newly issued one.

## Signing a build

`scripts/package.ps1` signs automatically when it is told how. Nothing else
changes.

**With a `.pfx`** (older certificates, or one exported from a token that allows
it):

```powershell
$env:LUMEN_SIGN_PFX = "C:\path\to\certificate.pfx"
$env:LUMEN_SIGN_PASSWORD = "…"
./scripts/package.ps1
```

**With a certificate in the Windows store** — the usual case for a hardware
token, since the private key never leaves it:

```powershell
$env:LUMEN_SIGN_THUMBPRINT = "A1B2C3…"
./scripts/package.ps1
```

Find the thumbprint with:

```powershell
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Format-List Subject, Thumbprint
```

Both the executable and the installer are signed, and the signature is
**verified** afterwards rather than assumed — `signtool sign` reports success
for signatures Windows will not actually accept, so `package.ps1` runs
`signtool verify /pa` and fails the build if it does not pass.

Signatures are timestamped against DigiCert's RFC 3161 server. Without a
timestamp, every build stops verifying the day the certificate expires.

## Signing in CI

The release workflow signs when two repository secrets exist:

| Secret | Contents |
|---|---|
| `WINDOWS_SIGNING_PFX_BASE64` | The `.pfx`, base64-encoded |
| `WINDOWS_SIGNING_PASSWORD` | Its password |

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes("certificate.pfx")) | Set-Clipboard
```

Without them the workflow warns and publishes an unsigned build rather than
failing — an unsigned release is worse than a signed one, but better than none.

A hardware token cannot be used this way. Signing on a GitHub-hosted runner
needs a cloud signing service (Azure Trusted Signing, SignPath, or a CA's own
HSM API); a token has to be signed against from a self-hosted runner or by hand.

## Verifying a downloaded build

Every release publishes `SHA256SUMS.txt`:

```powershell
(Get-FileHash .\LumenPDF-0.3.0-win64-setup.exe -Algorithm SHA256).Hash.ToLower()
```

and, if the build was signed:

```powershell
Get-AuthenticodeSignature .\LumenPDF-0.3.0-win64-setup.exe | Format-List Status, SignerCertificate
```

`Status` must be `Valid`. The in-app updater checks the checksum itself and
deletes the download if it does not match.
