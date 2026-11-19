param(
  [Parameter(Mandatory = $true)]
  [string]$Path,
  [Parameter(Mandatory = $true)]
  [string]$OutFile
)

$ErrorActionPreference = "Stop"
$resolved = (Resolve-Path -LiteralPath $Path).Path
$sha = [System.Security.Cryptography.SHA256]::Create()
$stream = [System.IO.File]::OpenRead($resolved)
try {
  $bytes = $sha.ComputeHash($stream)
  $hex = [System.BitConverter]::ToString($bytes).Replace("-", "").ToLowerInvariant()
  if ($hex.Length -ne 64) {
    throw "bad hash length $($hex.Length)"
  }
  $utf8 = New-Object System.Text.UTF8Encoding $false
  [System.IO.File]::WriteAllText($OutFile, $hex, $utf8)
} finally {
  $stream.Dispose()
  $sha.Dispose()
}
