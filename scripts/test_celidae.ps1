# Celidae acceptance tests.
#
#   .\scripts\test_celidae.ps1
#   .\scripts\test_celidae.ps1 -Quick          # skip the full example corpus
#   .\scripts\test_celidae.ps1 -Filter cluster # run one section
#
# Covers the four things that actually break Celidae:
#
#   1. A diagram type produces malformed output (unparseable JSON, invalid
#      XML, an unsubstituted template token). These fail silently at the
#      command line and only show up as a blank browser tab.
#   2. A view refers to data it never emitted - an edge pointing at a missing
#      node, a NaN where a number belongs. cytoscape throws on the first and
#      JSON.parse rejects the second, and either takes down the whole page.
#   3. The analysis layer computes something wrong. Statistics that are merely
#      plausible are worse than none, so the numbers are checked against a
#      fixture with known answers rather than only checked for well-formedness.
#   4. Malformed or hostile input crashes the binary instead of being reported.
#
# Exit code is 0 when everything passed, 1 otherwise, so CI can gate on it.

param(
    [string]$CelidaeExe = "build\celidae.exe",
    [string]$Filter = "",
    [switch]$Quick
)

$ErrorActionPreference = "Continue"

if (-not (Test-Path -LiteralPath $CelidaeExe)) {
    Write-Error "Missing $CelidaeExe. Build first, for example: .\build.cmd"
    exit 1
}

$script:Passed = 0
$script:Failed = 0
$script:Skipped = 0
$script:CurrentSection = ""

# Every DiagramType celidae exposes. Kept in one place so a type added in
# src/celidae/Visualization.h is covered by every section below at once - and
# so a type that disappears fails loudly here instead of quietly losing tests.
$AllTypes = @("schema", "graph", "er", "hierarchy", "timeline",
              "stats", "distribution", "comparison", "cluster")

# Placeholders the generator substitutes. Checked by exact name rather than by
# a /__[A-Z]+__/ pattern, because the bundled cytoscape and ECharts sources
# contain their own double-underscore identifiers (___EC__EXTENDED_CLASS___
# and friends) that a pattern match would report as leftover tokens.
$TemplateTokens = @("__TAILWIND_CSS__", "__CYTOSCAPE_JS__", "__ECHARTS_JS__") +
    ($AllTypes | ForEach-Object { "__DATA_$($_.ToUpper())__" })

function Start-Section {
    param([string]$Name)
    $script:CurrentSection = $Name
    if ($Filter -and $Name -notlike "*$Filter*") { return $false }
    Write-Host ""
    Write-Host "=== $Name ===" -ForegroundColor Cyan
    return $true
}

function Assert-True {
    param([bool]$Condition, [string]$Message, [string]$Detail = "")
    if ($Condition) {
        $script:Passed++
        Write-Host "  ok    $Message"
    } else {
        $script:Failed++
        Write-Host "  FAIL  $Message" -ForegroundColor Red
        if ($Detail) { Write-Host "        $Detail" -ForegroundColor DarkGray }
    }
}

function Assert-Equal {
    param($Expected, $Actual, [string]$Message)
    Assert-True ($Expected -eq $Actual) $Message "expected '$Expected', got '$Actual'"
}

function Invoke-Celidae {
    param([string[]]$Arguments)
    $output = & $CelidaeExe @Arguments
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Text     = ($output -join "`n")
    }
}

# Writes a UTF-8 file with NO byte-order mark. Set-Content -Encoding utf8 on
# Windows PowerShell 5.1 prepends one, and Felidae's lexer rejects a leading
# BOM with "Unexpected character at 1:1" - so a fixture written the obvious
# way fails every test for a reason that has nothing to do with Celidae.
function Write-Utf8NoBom {
    param([string]$Path, [string]$Content)
    [System.IO.File]::WriteAllText($Path, $Content, (New-Object System.Text.UTF8Encoding($false)))
}

# Runs celidae and captures stdout straight to a file. Piping a native
# command's output through Set-Content round-trips it through PowerShell's
# line splitting and re-encoding; for a 1.6 MB HTML export that is both slow
# and lossy, and the exit code gets lost behind the pipeline.
function Invoke-CelidaeToFile {
    param([string[]]$Arguments, [string]$Path)
    $errorPath = "$Path.err"
    $process = Start-Process -FilePath (Resolve-Path $CelidaeExe).Path `
        -ArgumentList $Arguments -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $Path -RedirectStandardError $errorPath
    $stderr = ""
    if (Test-Path -LiteralPath $errorPath) {
        $stderr = (Get-Content -LiteralPath $errorPath -Raw)
        Remove-Item -LiteralPath $errorPath -Force -ErrorAction SilentlyContinue
    }
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stderr   = $stderr
        Size     = if (Test-Path -LiteralPath $Path) { (Get-Item -LiteralPath $Path).Length } else { 0 }
    }
}

# ---------------------------------------------------------------------------
# A fixture whose answers are known by construction, generated rather than
# checked in so the expected values live beside the assertions that use them.
#
#   12 Reading records, one deliberate outlier (celsius 500 against a 18-24
#   band), two sensors, two clearly separated groups of load values.
# ---------------------------------------------------------------------------
$FixtureDir = Join-Path ([System.IO.Path]::GetTempPath()) ("celidae-tests-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force -Path $FixtureDir | Out-Null

$fixturePath = Join-Path $FixtureDir "fixture.fx"
Write-Utf8NoBom $fixturePath @'
# Generated by scripts/test_celidae.ps1 - expected values are asserted there.
Reading(sensor: "north", day: "2025-01-05", celsius: 19, load: 12)
Reading(sensor: "north", day: "2025-01-12", celsius: 21, load: 14)
Reading(sensor: "north", day: "2025-02-03", celsius: 20, load: 11)
Reading(sensor: "north", day: "2025-02-17", celsius: 22, load: 13)
Reading(sensor: "north", day: "2025-03-01", celsius: 18, load: 15)
Reading(sensor: "north", day: "2025-03-22", celsius: 23, load: 12)
Reading(sensor: "south", day: "2025-01-09", celsius: 24, load: 88)
Reading(sensor: "south", day: "2025-02-11", celsius: 22, load: 91)
Reading(sensor: "south", day: "2025-02-24", celsius: 23, load: 87)
Reading(sensor: "south", day: "2025-03-08", celsius: 21, load: 90)
Reading(sensor: "south", day: "2025-03-19", celsius: 20, load: 89)
Reading(sensor: "south", day: "2025-03-30", celsius: 500, load: 92)

Station(name: "north-yard")
Station extend Reading(sensor: "north", day: "2025-04-01", celsius: 19, load: 10)

main() =>
    system.print(value: "fixture")
    return
'@

$emptyPath = Join-Path $FixtureDir "empty.fx"
Write-Utf8NoBom $emptyPath ""

$malformedPath = Join-Path $FixtureDir "malformed.fx"
Write-Utf8NoBom $malformedPath @'
Broken(name: "unterminated
Another(( ))) =>
    where
extend extend extend
'@

$cyclicPath = Join-Path $FixtureDir "cyclic.fx"
Write-Utf8NoBom $cyclicPath @'
Alpha extend Beta(id: 1)
Beta extend Gamma(id: 2)
Gamma extend Alpha(id: 3)
'@

# ---------------------------------------------------------------------------
if (Start-Section "cli surface") {
    $version = Invoke-Celidae @("--version")
    Assert-Equal 0 $version.ExitCode "--version exits 0"
    $parsedVersion = $null
    try { $parsedVersion = $version.Text | ConvertFrom-Json } catch { }
    Assert-True ($null -ne $parsedVersion -and $parsedVersion.name -eq "celidae") "--version emits JSON naming celidae"

    $help = Invoke-Celidae @("--help")
    Assert-Equal 0 $help.ExitCode "--help exits 0"
    foreach ($type in $AllTypes) {
        Assert-True ($help.Text -match "(?m)^\s+$type\s") "--help documents the '$type' type"
    }

    $unknown = Invoke-Celidae @($fixturePath, "--type=nonsense")
    Assert-Equal 1 $unknown.ExitCode "an unknown --type is rejected"

    $missing = Invoke-Celidae @("no-such-file.fx", "--json")
    Assert-True ($missing.ExitCode -ne 0) "a missing input file is reported, not ignored"

    $wrongExtension = Invoke-Celidae @("README.md", "--json")
    Assert-Equal 1 $wrongExtension.ExitCode "a non-.fx input is rejected"
}

# ---------------------------------------------------------------------------
if (Start-Section "json payload integrity") {
    foreach ($type in $AllTypes) {
        $result = Invoke-Celidae @($fixturePath, "--json", "--type=$type")
        Assert-Equal 0 $result.ExitCode "$type : exits 0"

        $payload = $null
        try { $payload = $result.Text | ConvertFrom-Json } catch { }
        if ($null -eq $payload) {
            Assert-True $false "$type : output is valid JSON" $result.Text.Substring(0, [Math]::Min(200, $result.Text.Length))
            continue
        }
        Assert-True $true "$type : output is valid JSON"
        Assert-Equal $type $payload.mode "$type : reports its own mode"
        Assert-True ($payload.render -in @("network", "panels")) "$type : declares a known render mode"
        Assert-True ($null -ne $payload.palette) "$type : carries the node palette"

        # Duplicate ids would silently drop nodes in cytoscape; a dangling edge
        # endpoint makes it throw and abandon the view entirely.
        $ids = @($payload.nodes | ForEach-Object { $_.id })
        $unique = @($ids | Select-Object -Unique)
        Assert-Equal $ids.Count $unique.Count "$type : node ids are unique"

        $idSet = @{}
        foreach ($id in $ids) { $idSet[$id] = $true }
        $dangling = @($payload.edges | Where-Object { -not $idSet.ContainsKey($_.from) -or -not $idSet.ContainsKey($_.to) })
        Assert-Equal 0 $dangling.Count "$type : every edge endpoint resolves to a node"

        # NaN and Infinity are not JSON. If a statistic divides by zero and the
        # serializer lets it through, the browser rejects the whole document.
        Assert-True ($result.Text -notmatch "\b(nan|NaN|-?inf|Infinity)\b") "$type : contains no NaN/Infinity literals"

        foreach ($panel in $payload.panels) {
            Assert-True (-not [string]::IsNullOrWhiteSpace($panel.type)) "$type : panel '$($panel.title)' declares a type"
            if ($panel.type -in @("bar", "hbar", "histogram", "line")) {
                foreach ($series in $panel.series) {
                    Assert-Equal $panel.categories.Count $series.values.Count "$type : panel '$($panel.title)' series '$($series.name)' matches its category count"
                }
            }
        }
    }
}

# ---------------------------------------------------------------------------
if (Start-Section "analysis correctness") {
    $stats = (Invoke-Celidae @($fixturePath, "--json", "--type=stats")).Text | ConvertFrom-Json
    $reading = $stats.nodes | Where-Object { $_.id -eq "fact:Reading" }
    Assert-True ($null -ne $reading) "stats : the Reading fact type is present"
    Assert-Equal 12 $reading.metrics.records "stats : counts all 12 Reading records"
    Assert-Equal 4 $reading.metrics.fields "stats : counts all 4 Reading fields"

    # 'day' is an ISO date, 'celsius'/'load' are numeric, 'sensor' has two
    # levels across twelve records so it is a category, not a key.
    $schema = (Invoke-Celidae @($fixturePath, "--json", "--type=schema")).Text | ConvertFrom-Json
    $fieldTypes = @{}
    foreach ($node in $schema.nodes | Where-Object { $_.kind -eq "field" -and $_.attributes.factType -eq "Reading" }) {
        $fieldTypes[$node.label] = $node.attributes.type
    }
    Assert-Equal "date" $fieldTypes["day"] "schema : 'day' is detected as a date"
    Assert-Equal "numeric" $fieldTypes["celsius"] "schema : 'celsius' is detected as numeric"
    Assert-Equal "categorical" $fieldTypes["sensor"] "schema : 'sensor' is detected as categorical"

    # The 500 reading sits far outside an 18-24 band; a robust outlier test
    # must find it. A stddev-based test would not - one value that extreme
    # inflates the standard deviation enough to hide itself.
    $distribution = (Invoke-Celidae @($fixturePath, "--json", "--type=distribution")).Text | ConvertFrom-Json
    $celsius = $distribution.nodes | Where-Object { $_.id -eq "field:Reading.celsius" }
    Assert-True ($null -ne $celsius -and $celsius.metrics.outliers -ge 1) "distribution : flags the celsius outlier" "outliers=$($celsius.metrics.outliers)"
    Assert-True (@($distribution.insights | Where-Object { $_.text -match "500" }).Count -ge 1) "distribution : names the outlying value in a finding"
    Assert-True (@($distribution.panels | Where-Object { $_.type -eq "histogram" }).Count -ge 1) "distribution : emits a histogram"

    # Two sensors with cleanly separated load bands: k-means should find two
    # groups, and the silhouette should say they are genuinely separated.
    $cluster = (Invoke-Celidae @($fixturePath, "--json", "--type=cluster")).Text | ConvertFrom-Json
    $clusterFact = $cluster.nodes | Where-Object { $_.id -eq "fact:Reading" }
    Assert-True ($null -ne $clusterFact) "cluster : produced segments for Reading"
    if ($null -ne $clusterFact) {
        Assert-Equal 2 $clusterFact.metrics.segments "cluster : finds exactly the two sensor populations"
        Assert-True ($clusterFact.metrics.separation -gt 0.4) "cluster : reports well-separated segments" "separation=$($clusterFact.metrics.separation)"
    }
    $scatter = @($cluster.panels | Where-Object { $_.type -eq "scatter" })
    Assert-True ($scatter.Count -ge 1) "cluster : emits a scatter panel"
    if ($scatter.Count -ge 1) {
        Assert-Equal 12 $scatter[0].points.Count "cluster : plots every record"
    }

    # Correlation: load tracks the sensor, so the encoded columns must
    # correlate. Complementary levels of one field must NOT be reported -
    # sensor=north falling whenever sensor=south rises is the encoding
    # talking, not the data.
    $comparison = (Invoke-Celidae @($fixturePath, "--json", "--type=comparison")).Text | ConvertFrom-Json
    Assert-True (@($comparison.panels | Where-Object { $_.type -eq "heatmap" }).Count -ge 1) "comparison : emits a correlation heatmap"
    $selfPairs = @($comparison.insights | Where-Object { $_.text -match "sensor=\w+ and sensor=" })
    Assert-Equal 0 $selfPairs.Count "comparison : does not report one field's own levels as correlated"

    # Timeline: all 13 dated records (12 Reading + 1 Station) ordered by day,
    # grouped into the single year they fall in.
    $timeline = (Invoke-Celidae @($fixturePath, "--json", "--type=timeline")).Text | ConvertFrom-Json
    $events = @($timeline.nodes | Where-Object { $_.kind -eq "event" })
    Assert-Equal 13 $events.Count "timeline : places every dated record"
    $sequences = @($events | Where-Object { $_.attributes.factType -eq "Reading" } | ForEach-Object { $_.metrics.sequence })
    Assert-Equal 12 $sequences.Count "timeline : sequences every Reading record"

    # Hierarchy: Station extends Reading, so Reading is a root and Station is not.
    $hierarchy = (Invoke-Celidae @($fixturePath, "--json", "--type=hierarchy")).Text | ConvertFrom-Json
    $station = $hierarchy.nodes | Where-Object { $_.id -eq "fact:Station" }
    Assert-True ($null -ne $station -and $station.metrics.depth -eq 1) "hierarchy : Station sits one level below its parent" "depth=$($station.metrics.depth)"
    Assert-True (@($hierarchy.panels | Where-Object { $_.type -eq "treemap" }).Count -ge 1) "hierarchy : emits a treemap"
}

# ---------------------------------------------------------------------------
if (Start-Section "view recommendation") {
    $recommend = Invoke-Celidae @($fixturePath, "--recommend")
    Assert-Equal 0 $recommend.ExitCode "--recommend exits 0"
    $parsed = $null
    try { $parsed = $recommend.Text | ConvertFrom-Json } catch { }
    Assert-True ($null -ne $parsed) "--recommend emits valid JSON"
    if ($null -ne $parsed) {
        Assert-True ($parsed.recommendations.Count -ge 1) "--recommend suggests at least one view"
        foreach ($entry in $parsed.recommendations) {
            Assert-True ($AllTypes -contains $entry.view) "recommends a real diagram type ('$($entry.view)')"
            Assert-True ($entry.score -gt 0 -and $entry.score -le 1) "'$($entry.view)' scores within 0..1" "score=$($entry.score)"
            Assert-True (-not [string]::IsNullOrWhiteSpace($entry.rationale)) "'$($entry.view)' explains itself"
        }
        # Scores must be ordered, since the page shows recommendations[0].
        $scores = @($parsed.recommendations | ForEach-Object { $_.score })
        $sorted = @($scores | Sort-Object -Descending)
        Assert-True (@(Compare-Object $scores $sorted -SyncWindow 0).Count -eq 0) "recommendations come back ranked"
        # This fixture has clusterable data, so cluster must be the top pick.
        Assert-Equal "cluster" $parsed.recommendations[0].view "the strongest view for this data is recommended first"
    }

    # A program with no fact data must not recommend a data view.
    $structural = Join-Path $FixtureDir "structural.fx"
    Write-Utf8NoBom $structural @'
Thing(name: name)
work() =>
    Thing(name: n)
    return
'@
    $none = (Invoke-Celidae @($structural, "--recommend")).Text | ConvertFrom-Json
    $dataViews = @($none.recommendations | Where-Object { $_.view -in @("cluster", "distribution", "comparison") })
    Assert-Equal 0 $dataViews.Count "a program with no literal values recommends no data view"
}

# ---------------------------------------------------------------------------
if (Start-Section "html export") {
    $htmlPath = Join-Path $FixtureDir "all.html"
    $run = Invoke-CelidaeToFile @($fixturePath, "--html") $htmlPath
    Assert-Equal 0 $run.ExitCode "--html exits 0" $run.Stderr
    Assert-True ($run.Size -gt 100000) "--html writes a self-contained page" "size=$($run.Size)"

    $html = Get-Content -LiteralPath $htmlPath -Raw
    foreach ($token in $TemplateTokens) {
        Assert-True (-not $html.Contains($token)) "--html substitutes $token"
    }
    Assert-True ($html.Contains("<script>") -and $html.Contains("echarts")) "--html bundles its charting library"
    Assert-True ($html.Contains("cytoscape")) "--html bundles its graph library"

    # Every view's payload must be present and parseable; the page reads them
    # out of these script tags at load.
    foreach ($type in $AllTypes) {
        $pattern = "(?s)<script type=""application/json"" id=""data-$type"">(.*?)</script>"
        $match = [regex]::Match($html, $pattern)
        Assert-True $match.Success "--html embeds the $type payload"
        if ($match.Success) {
            $ok = $true
            try { $null = $match.Groups[1].Value | ConvertFrom-Json } catch { $ok = $false }
            Assert-True $ok "--html embeds parseable JSON for $type"
        }
    }

    # --template=<name> must emit that one view and null out the rest, which
    # is what makes a single-view export single-view.
    foreach ($type in $AllTypes) {
        $singlePath = Join-Path $FixtureDir "one-$type.html"
        $run = Invoke-CelidaeToFile @($fixturePath, "--html", "--template=$type") $singlePath
        Assert-Equal 0 $run.ExitCode "--template=$type exits 0" $run.Stderr
        if ($run.ExitCode -ne 0) { continue }
        $single = Get-Content -LiteralPath $singlePath -Raw
        $nulls = 0
        foreach ($other in $AllTypes) {
            $match = [regex]::Match($single, "(?s)<script type=""application/json"" id=""data-$other"">(.*?)</script>")
            if ($match.Success -and $match.Groups[1].Value.Trim() -eq "null") { $nulls++ }
        }
        Assert-Equal ($AllTypes.Count - 1) $nulls "--template=$type carries only its own payload"
    }
}

# ---------------------------------------------------------------------------
if (Start-Section "svg export") {
    # An SVG that is not well-formed XML renders as nothing at all, and an
    # unescaped '&' or '<' in a fact label is all it takes. [xml] on an empty
    # or missing file yields $null without throwing, so the size is checked
    # too - otherwise a view that produced nothing would pass this test.
    function Test-Svg {
        param([string]$Path, [string]$Message)
        $size = if (Test-Path -LiteralPath $Path) { (Get-Item -LiteralPath $Path).Length } else { 0 }
        if ($size -le 0) { Assert-True $false $Message "no output produced"; return }
        try {
            $document = [xml](Get-Content -LiteralPath $Path -Raw)
            Assert-True ($null -ne $document.svg) $Message "parsed, but has no <svg> root"
        } catch {
            Assert-True $false $Message $_.Exception.Message
        }
    }

    foreach ($type in $AllTypes) {
        $svgPath = Join-Path $FixtureDir "$type.svg"
        $run = Invoke-CelidaeToFile @($fixturePath, "--svg", "--type=$type") $svgPath
        Assert-Equal 0 $run.ExitCode "--svg --type=$type exits 0" $run.Stderr
        Test-Svg $svgPath "--svg --type=$type produces well-formed XML"
    }

    # A label containing XML metacharacters is the case that actually breaks.
    $hostilePath = Join-Path $FixtureDir "hostile.fx"
    Write-Utf8NoBom $hostilePath @'
Weird(name: "a & b", note: "<script>x</script>", quote: "angle > bracket")
Weird(name: "c < d", note: "]]>", quote: "&amp;")
'@
    foreach ($type in @("schema", "distribution")) {
        $svgPath = Join-Path $FixtureDir "hostile-$type.svg"
        $null = Invoke-CelidaeToFile @($hostilePath, "--svg", "--type=$type") $svgPath
        Test-Svg $svgPath "--svg escapes XML metacharacters in labels ($type)"
    }

    # The same characters must not break out of the JSON payload or the
    # surrounding <script> element in the HTML export.
    $hostileHtml = Join-Path $FixtureDir "hostile.html"
    $null = Invoke-CelidaeToFile @($hostilePath, "--html") $hostileHtml
    $raw = Get-Content -LiteralPath $hostileHtml -Raw
    $match = [regex]::Match($raw, "(?s)<script type=""application/json"" id=""data-schema"">(.*?)</script>")
    Assert-True $match.Success "--html keeps a hostile label inside its payload element"
    if ($match.Success) {
        $ok = $true
        try { $null = $match.Groups[1].Value | ConvertFrom-Json } catch { $ok = $false }
        Assert-True $ok "--html payload survives a label containing </script>"
    }
}

# ---------------------------------------------------------------------------
if (Start-Section "determinism") {
    # Clustering seeds its RNG with a constant precisely so this holds. A
    # random seed would renumber segments on every run, and a re-generated
    # report would diff against its predecessor for no reason.
    foreach ($type in @("cluster", "distribution", "comparison", "timeline")) {
        $first = (Invoke-Celidae @($fixturePath, "--json", "--type=$type")).Text
        $second = (Invoke-Celidae @($fixturePath, "--json", "--type=$type")).Text
        Assert-True ($first -ceq $second) "$type : two runs produce byte-identical output"
    }
}

# ---------------------------------------------------------------------------
if (Start-Section "robustness") {
    # Every one of these must be reported, not crash. On Windows an unhandled
    # access violation surfaces as a large negative or 0xC-prefixed exit code,
    # never as 0 or 1.
    $hostileInputs = @(
        @{ Name = "empty file"; Path = $emptyPath },
        @{ Name = "malformed source"; Path = $malformedPath },
        @{ Name = "cyclic extend chain"; Path = $cyclicPath }
    )
    # Not $input as the loop variable: that is an automatic variable holding
    # the pipeline enumerator, and assigning to it inside a function breaks
    # the pipeline in ways that are very hard to trace back to here.
    foreach ($case in $hostileInputs) {
        foreach ($type in $AllTypes) {
            $result = Invoke-Celidae @($case.Path, "--json", "--type=$type")
            Assert-True ($result.ExitCode -eq 0 -or $result.ExitCode -eq 1) "$($case.Name) / $type : handled, exit $($result.ExitCode)"
        }
    }

    # A cyclic extend chain is the case that can hang rather than crash: the
    # depth walk has to notice it is going in circles.
    $job = Start-Job -ScriptBlock {
        param($exe, $path)
        & $exe $path --html | Out-Null
        $LASTEXITCODE
    } -ArgumentList ((Resolve-Path $CelidaeExe).Path, $cyclicPath)
    $finished = Wait-Job $job -Timeout 30
    Assert-True ($null -ne $finished) "a cyclic extend chain terminates rather than hanging"
    Remove-Job $job -Force -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------------------------
if (-not $Quick) {
    if (Start-Section "example corpus sweep") {
        $examples = @(Get-ChildItem -Path "examples", "v2_examples" -Filter "*.fx" -ErrorAction SilentlyContinue)
        Assert-True ($examples.Count -gt 0) "found example programs to sweep"
        $broken = @()
        foreach ($example in $examples) {
            foreach ($type in $AllTypes) {
                $result = Invoke-Celidae @($example.FullName, "--json", "--type=$type")
                if ($result.ExitCode -ne 0 -and $result.ExitCode -ne 1) {
                    $broken += "$($example.Name) --type=$type exit $($result.ExitCode)"
                    continue
                }
                if ($result.ExitCode -ne 0) { continue }
                try { $null = $result.Text | ConvertFrom-Json } catch {
                    $broken += "$($example.Name) --type=$type produced unparseable JSON"
                }
            }
        }
        Assert-Equal 0 $broken.Count "every example renders every diagram type as valid JSON"
        foreach ($failure in $broken | Select-Object -First 15) {
            Write-Host "        $failure" -ForegroundColor DarkGray
        }
        Write-Host "        swept $($examples.Count) programs x $($AllTypes.Count) types"
    }
} else {
    $script:Skipped++
    Write-Host ""
    Write-Host "  skip  example corpus sweep (-Quick)" -ForegroundColor DarkGray
}

# ---------------------------------------------------------------------------
Remove-Item -LiteralPath $FixtureDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
if ($script:Failed -eq 0) {
    Write-Host "$($script:Passed) passed, 0 failed" -ForegroundColor Green
    exit 0
} else {
    Write-Host "$($script:Passed) passed, $($script:Failed) FAILED" -ForegroundColor Red
    exit 1
}
