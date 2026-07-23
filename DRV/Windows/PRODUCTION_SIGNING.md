# Windows production signing

The checked-in Release configuration does not create or embed a WDK test
signature. Microsoft supplies the production signature after a successful
Hardware Dev Center submission.

## Create the release inputs

From PowerShell in this directory:

```powershell
.\package-release.ps1
```

This performs a clean unsigned x64 Release build and creates:

- `submission\TimeCard\` - the driver folder to add to an HLK project.
- `submission\TimeCard-1.17-x64.cab` - a CAB with the required driver,
  INF, catalog, subsystem icons, and PDB in a non-root `TimeCard` folder.

The CAB is intentionally unsigned unless a registered code-signing
certificate and its provider's RFC 3161 timestamp service are supplied:

```powershell
.\package-release.ps1 `
    -CertificateThumbprint '<SHA-1 thumbprint>' `
    -TimestampUrl '<certificate-provider timestamp URL>'
```

The certificate's private key should remain in its approved hardware token,
HSM, or managed signing service. Do not store a PFX or password in this
repository.

## WHCP/HLK route for production distribution

Use this route for a retail driver and for Windows Update eligibility:

1. Associate a valid EV code-signing certificate with the organization's
   Hardware Developer Program account.
2. Install the current Windows HLK that corresponds to every Windows release
   the driver will target.
3. Connect the Time Card test machine as an HLK Client and run the device and
   driver certification playlist selected by HLK Studio.
4. In HLK Studio's Package tab, add `submission\TimeCard\` as the driver
   folder and add `timecard.pdb` as its symbols.
5. Create and sign the `.hlkx` package using a certificate registered to the
   Partner Center organization.
6. Upload the `.hlkx` package through **Submit new hardware**, request the
   applicable x64 Windows signatures, and leave test-signing disabled.
7. Download Microsoft's signed package, verify its SYS and CAT signatures,
   and install that exact returned package on a Secure Boot-enabled machine.

## Attestation route

Microsoft currently limits attestation signing to supported testing
scenarios; attestation-signed retail drivers are not published to Windows
Update. When the Partner Center account is eligible for such a scenario,
upload the certificate-signed `TimeCard-1.17-x64.cab`, leave both test-signing
options disabled, and download the Microsoft-signed result.

## Verify the returned package

Use the x64 WDK SignTool from an Administrator developer prompt:

```bat
signtool verify /kp /v timecard.sys
signtool verify /kp /v /c timecard.cat timecard.sys
```

The production package must validate against the kernel-mode policy and show
a Microsoft signer. Keep the Hardware Dev Center submission report with the
released binaries.
