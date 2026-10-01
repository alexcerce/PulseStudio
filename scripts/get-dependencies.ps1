# Fetches the third-party code into external/ at the exact versions this project is built against.
# Run from the repository root:  powershell -ExecutionPolicy Bypass -File scripts\get-dependencies.ps1
$ErrorActionPreference = "Stop"
$ext = Join-Path $PSScriptRoot "..\external"
New-Item -ItemType Directory -Force $ext | Out-Null

function Get-Repo ($url, $dir, $commit) {
    if (-not (Test-Path (Join-Path $dir ".git"))) { git clone $url $dir }
    git -C $dir fetch origin $commit
    git -C $dir checkout $commit
    # submodules use SSH URLs in some repos; force HTTPS
    git -C $dir -c url."https://github.com/".insteadOf="git@github.com:" submodule update --init --recursive
}

Get-Repo "https://github.com/Tracktion/tracktion_engine.git" (Join-Path $ext "tracktion_engine") "9f7059c063127a3ee98032a6cdaf2d7a59d45455"
# JUCE comes in as tracktion_engine's submodule (37c894f83d379179b2070d437ccd0f1cd9af9576)
