param(
    [int[]] $Sizes = @(10000, 100000, 1000000),
    [int] $QueryRepeats = 100,
    [string] $Exe = "",
    [string] $Output = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exePath = if ($Exe) { $Exe } else { Join-Path $root "build\felidae.exe" }
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Missing $exePath. Build a production executable first."
}
if ($QueryRepeats -lt 2) { throw "QueryRepeats must be at least 2." }

$runDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("felidae-scale-" + [Guid]::NewGuid().ToString("N"))
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null
if (-not $Output) { $Output = Join-Path $root "build\fact-scale-baseline.csv" }

function New-FactWorkload([int] $Count, [string] $Path) {
    $writer = [System.IO.StreamWriter]::new($Path, $false, [System.Text.UTF8Encoding]::new($false))
    try {
        for ($id = 0; $id -lt $Count; ++$id) {
            $group = "g" + ($id % 100)
            $active = if (($id % 2) -eq 0) { "true" } else { "false" }
            $writer.WriteLine(('BenchmarkFact(id: {0}, group: "{1}", active: {2})' -f $id, $group, $active))
        }
    } finally {
        $writer.Dispose()
    }
}

function Invoke-Felidae([string] $File) {
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo.FileName = $exePath
    $process.StartInfo.WorkingDirectory = $root
    $query = '? BenchmarkFact(group: "g42", active: true, id: Id)'
    $process.StartInfo.Arguments = ('"{0}" "{1}" --benchmark-repeat {2} --metrics-json' -f $File, $query.Replace('"', '\"'), $QueryRepeats)
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    [void] $process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $peakBytes = 0L
    while (-not $process.HasExited) {
        $process.Refresh()
        $peakBytes = [Math]::Max($peakBytes, $process.WorkingSet64)
        Start-Sleep -Milliseconds 5
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) { throw "Felidae failed: $stderr$stdout" }
    if ($stdout -notmatch 'Id = 42') { throw "Generated workload returned an unexpected result: $stdout" }
    $match = [regex]::Match($stderr, 'FELIDAE_METRICS (\{.*\})')
    if (-not $match.Success) { throw "Missing FELIDAE_METRICS: $stderr" }
    return [pscustomobject]@{ PeakMb = $peakBytes / 1MB; Metrics = $match.Groups[1].Value | ConvertFrom-Json }
}

try {
    $results = foreach ($size in $Sizes) {
        if ($size -lt 1) { throw "Fact counts must be positive." }
        $file = Join-Path $runDirectory ("facts-$size.fx")
        Write-Host "Generating $size facts..."
        New-FactWorkload -Count $size -Path $file
        Write-Host "Loading and querying $size facts..."
        $sample = Invoke-Felidae -File $file
        [pscustomobject]@{
            Facts = $size
            LoadMs = $sample.Metrics.loadMs
            FirstQueryMs = $sample.Metrics.firstQueryMs
            RepeatedQueryMs = $sample.Metrics.repeatedQueryAverageMs
            PeakMb = $sample.PeakMb
            FactCandidates = $sample.Metrics.runtime.factCandidates
            UnificationAttempts = $sample.Metrics.runtime.unificationAttempts
            ParserTokens = $sample.Metrics.runtime.parserTokensLexed
        }
    }
    $results | Export-Csv -NoTypeInformation -Path $Output
    $results | Format-Table -AutoSize
    Write-Host "Baseline written to $Output"
} finally {
    if (Test-Path -LiteralPath $runDirectory) { Remove-Item -LiteralPath $runDirectory -Recurse -Force }
}
