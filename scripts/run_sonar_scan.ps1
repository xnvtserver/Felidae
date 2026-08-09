[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Token,
    [switch]$StartOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compose = Join-Path $root 'docker-compose.sonar.yml'

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw 'Docker Desktop or Docker Engine is required for SonarQube analysis.'
}

Push-Location $root
try {
    docker compose -f $compose up -d sonarqube
    if ($LASTEXITCODE -ne 0) { throw 'Unable to start SonarQube.' }

    if ($StartOnly) {
        Write-Host 'SonarQube is starting at http://localhost:9000'
        Write-Host 'Create a project token there, then run this script again without -StartOnly.'
        return
    }

    if ([string]::IsNullOrWhiteSpace($Token) -or $Token -eq 'placeholder') {
        throw 'A SonarQube project token is required unless -StartOnly is used.'
    }

    $env:SONAR_TOKEN = $Token
    docker compose -f $compose --profile scan run --rm sonar-scanner
    if ($LASTEXITCODE -ne 0) { throw 'SonarQube analysis failed.' }
} finally {
    Remove-Item Env:SONAR_TOKEN -ErrorAction SilentlyContinue
    Pop-Location
}
