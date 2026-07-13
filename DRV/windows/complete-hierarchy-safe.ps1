#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [string]$ExpectedInf = 'oem233.inf',
    [string]$FallbackInf = 'oem226.inf'
)

$ErrorActionPreference = 'Stop'

try {
    & (Join-Path $PSScriptRoot 'enable-hierarchy-safe.ps1') `
        -ExpectedInf $ExpectedInf
    & (Join-Path $PSScriptRoot 'persist-hierarchy-safe.ps1') `
        -ExpectedInf $ExpectedInf -FallbackInf $FallbackInf
}
catch {
    Write-Error "Safe hierarchy completion failed: $_"
    exit 1
}

exit 0
