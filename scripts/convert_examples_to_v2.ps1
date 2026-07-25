param([string]$Root = "examples")

$ErrorActionPreference = "Stop"

function Get-CodeDepth {
    param([string]$Line, [int]$InitialDepth)
    $depth = $InitialDepth
    $quoted = $false
    $escaped = $false
    for ($index = 0; $index -lt $Line.Length; $index++) {
        $character = $Line[$index]
        if ($quoted) {
            if ($escaped) { $escaped = $false; continue }
            if ($character -eq '\') { $escaped = $true; continue }
            if ($character -eq '"') { $quoted = $false }
            continue
        }
        if ($character -eq '#') { break }
        if ($character -eq '"') { $quoted = $true; continue }
        if ($character -in @('(', '[', '{')) { $depth++ }
        if ($character -in @(')', ']', '}')) { $depth-- }
    }
    return $depth
}

Get-ChildItem -LiteralPath $Root -Recurse -File -Filter *.fx | ForEach-Object {
    $path = $_.FullName
    $lines = [System.IO.File]::ReadAllLines($path)
    $depth = 0
    $changed = $false
    for ($lineIndex = 0; $lineIndex -lt $lines.Length; $lineIndex++) {
        $line = $lines[$lineIndex]
        $commentAt = -1
        $quoted = $false
        $escaped = $false
        for ($index = 0; $index -lt $line.Length; $index++) {
            $character = $line[$index]
            if ($quoted) {
                if ($escaped) { $escaped = $false; continue }
                if ($character -eq '\') { $escaped = $true; continue }
                if ($character -eq '"') { $quoted = $false }
                continue
            }
            if ($character -eq '"') { $quoted = $true; continue }
            if ($character -eq '#') { $commentAt = $index; break }
        }
        $code = if ($commentAt -ge 0) { $line.Substring(0, $commentAt) } else { $line }
        $comment = if ($commentAt -ge 0) { $line.Substring($commentAt) } else { "" }
        $trimmed = $code.TrimEnd()

        if ($trimmed.EndsWith('.')) {
            $trimmed = $trimmed.Substring(0, $trimmed.Length - 1)
            $changed = $true
        }

        $candidateDepth = Get-CodeDepth -Line $trimmed -InitialDepth $depth
        if ($trimmed.EndsWith(',') -and $candidateDepth -eq 0) {
            $trimmed = $trimmed.Substring(0, $trimmed.Length - 1)
            $changed = $true
        }

        $spacing = if ($comment.Length -gt 0 -and $trimmed.Length -gt 0) { " " } else { "" }
        $lines[$lineIndex] = $trimmed + $spacing + $comment
        $depth = Get-CodeDepth -Line $trimmed -InitialDepth $depth
    }
    if ($changed) {
        [System.IO.File]::WriteAllLines($path, $lines, [System.Text.UTF8Encoding]::new($false))
    }
}
