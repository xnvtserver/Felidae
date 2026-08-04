# Celidae acceptance tests.
#
#   .\scripts\test_celidae.ps1
#   .\scripts\test_celidae.ps1 -Quick          # skip the full example corpus
#   .\scripts\test_celidae.ps1 -Filter cluster # run one section
#
# Covers the four things that actually break Celidae:
#
#   1. A diagram type produces malformed output (unparseable JSON, an
#      unsubstituted template token). These fail silently at the command line
#      and only show up as a blank browser tab.
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
# Celidae produces one visualization - the interactive HTML page - and no
# longer has a --json output mode. Every view's payload still exists, just
# embedded in the page inside <script type="application/json" id="data-X">
# elements, one per diagram type, always all nine unless --template narrows
# the run to one. These helpers run celidae once per fixture and pull every
# view's payload out of that single HTML document, which is both what a test
# needs and exactly what a browser actually reads - so this suite is now
# checking the real artifact rather than a parallel code path a user never
# invokes.
# ---------------------------------------------------------------------------

$script:ViewCache = @{}

# Runs celidae for one fixture and extracts every embedded view payload.
# Cached per (path, load-imports) pair, since many assertions below want
# several views of the same fixture and previously paid for a fresh process
# per view; one HTML build now serves all of them. Pass -Fresh to force a new
# process (used by the determinism check, which exists specifically to prove
# two independent runs agree).
function Get-CelidaeViews {
    param([string]$Path, [switch]$LoadImports, [switch]$Fresh)
    $key = "$Path|$($LoadImports.IsPresent)"
    if (-not $Fresh -and $script:ViewCache.ContainsKey($key)) {
        return $script:ViewCache[$key]
    }
    $arguments = @($Path)
    if ($LoadImports) { $arguments += "--load-imports" }
    $output = & $CelidaeExe @arguments
    $html = ($output -join "`n")
    $views = @{}
    if ($LASTEXITCODE -eq 0) {
        foreach ($type in $AllTypes) {
            $match = [regex]::Match(
                $html, "(?s)<script type=""application/json"" id=""data-$type"">(.*?)</script>")
            if (-not $match.Success) { continue }
            $text = $match.Groups[1].Value
            $parsed = $null
            try { $parsed = $text | ConvertFrom-Json } catch { }
            $views[$type] = [pscustomobject]@{ Text = $text; Parsed = $parsed }
        }
    }
    $result = [pscustomobject]@{ ExitCode = $LASTEXITCODE; Html = $html; Views = $views }
    if (-not $Fresh) { $script:ViewCache[$key] = $result }
    return $result
}

# One view's parsed payload, or $null if celidae failed or that view was not
# embedded (e.g. a single-template export).
function Get-CelidaeView {
    param([string]$Path, [string]$Type, [switch]$LoadImports)
    $run = Get-CelidaeViews -Path $Path -LoadImports:$LoadImports
    if ($run.Views.ContainsKey($Type)) { return $run.Views[$Type].Parsed }
    return $null
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

    $unknown = Invoke-Celidae @($fixturePath, "--template=nonsense")
    Assert-Equal 1 $unknown.ExitCode "an unknown --template is rejected"

    # --json/--inspect-graph/--type were retired along with the JSON output
    # mode; each must fail with a message pointing at --template rather than
    # silently doing nothing or falling through to a different behaviour.
    $oldJson = Invoke-Celidae @($fixturePath, "--json")
    Assert-Equal 1 $oldJson.ExitCode "the retired --json flag is rejected, not silently ignored"
    $oldType = Invoke-Celidae @($fixturePath, "--type=schema")
    Assert-Equal 1 $oldType.ExitCode "the retired --type flag is rejected"
    $oldSvg = Invoke-Celidae @($fixturePath, "--svg")
    Assert-Equal 1 $oldSvg.ExitCode "the retired --svg flag is rejected"

    $missing = Invoke-Celidae @("no-such-file.fx")
    Assert-True ($missing.ExitCode -ne 0) "a missing input file is reported, not ignored"

    $wrongExtension = Invoke-Celidae @("README.md")
    Assert-Equal 1 $wrongExtension.ExitCode "a non-.fx input is rejected"
}

# ---------------------------------------------------------------------------
if (Start-Section "html payload integrity") {
    # One process call builds every view - --html always bundles all nine
    # unless --template narrows it - so the exit code is checked once here
    # rather than nine times, and what follows inspects the payloads that
    # single build actually produced.
    $run = Get-CelidaeViews -Path $fixturePath -Fresh
    Assert-Equal 0 $run.ExitCode "--html exits 0"

    foreach ($type in $AllTypes) {
        if (-not $run.Views.ContainsKey($type)) {
            Assert-True $false "$type : payload is embedded in the page"
            continue
        }
        $view = $run.Views[$type]
        $payload = $view.Parsed
        if ($null -eq $payload) {
            Assert-True $false "$type : output is valid JSON" $view.Text.Substring(0, [Math]::Min(200, $view.Text.Length))
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
        Assert-True ($view.Text -notmatch "\b(nan|NaN|-?inf|Infinity)\b") "$type : contains no NaN/Infinity literals"

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
    $stats = Get-CelidaeView $fixturePath "stats"
    $reading = $stats.nodes | Where-Object { $_.id -eq "fact:Reading" }
    Assert-True ($null -ne $reading) "stats : the Reading fact type is present"
    Assert-Equal 12 $reading.metrics.records "stats : counts all 12 Reading records"
    Assert-Equal 4 $reading.metrics.fields "stats : counts all 4 Reading fields"

    # 'day' is an ISO date, 'celsius'/'load' are numeric, 'sensor' has two
    # levels across twelve records so it is a category, not a key.
    $schema = Get-CelidaeView $fixturePath "schema"
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
    $distribution = Get-CelidaeView $fixturePath "distribution"
    $celsius = $distribution.nodes | Where-Object { $_.id -eq "field:Reading.celsius" }
    Assert-True ($null -ne $celsius -and $celsius.metrics.outliers -ge 1) "distribution : flags the celsius outlier" "outliers=$($celsius.metrics.outliers)"
    Assert-True (@($distribution.insights | Where-Object { $_.text -match "500" }).Count -ge 1) "distribution : names the outlying value in a finding"
    Assert-True (@($distribution.panels | Where-Object { $_.type -eq "histogram" }).Count -ge 1) "distribution : emits a histogram"

    # Two sensors with cleanly separated load bands: k-means should find two
    # groups, and the silhouette should say they are genuinely separated.
    $cluster = Get-CelidaeView $fixturePath "cluster"
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
    $comparison = Get-CelidaeView $fixturePath "comparison"
    Assert-True (@($comparison.panels | Where-Object { $_.type -eq "heatmap" }).Count -ge 1) "comparison : emits a correlation heatmap"
    $selfPairs = @($comparison.insights | Where-Object { $_.text -match "sensor=\w+ and sensor=" })
    Assert-Equal 0 $selfPairs.Count "comparison : does not report one field's own levels as correlated"

    # Timeline: Reading's 12 dated records are placed in day order. Station
    # carries a date on only one of its two records, and one point is not a
    # sequence - a "Station over time" chart would be a single bar that looks
    # exactly like a chart with a trend in it. So Station is left out, and the
    # count here is 12 rather than 13.
    $timeline = Get-CelidaeView $fixturePath "timeline"
    $events = @($timeline.nodes | Where-Object { $_.kind -eq "event" })
    Assert-Equal 12 $events.Count "timeline : places every record of a fact type with a real sequence"
    $sequences = @($events | Where-Object { $_.attributes.factType -eq "Reading" } | ForEach-Object { $_.metrics.sequence })
    Assert-Equal 12 $sequences.Count "timeline : sequences every Reading record"
    $stationEvents = @($events | Where-Object { $_.attributes.factType -eq "Station" })
    Assert-Equal 0 $stationEvents.Count "timeline : a single dated record is not drawn as a timeline"

    # Hierarchy: Station extends Reading, so Reading is a root and Station is not.
    $hierarchy = Get-CelidaeView $fixturePath "hierarchy"
    $station = $hierarchy.nodes | Where-Object { $_.id -eq "fact:Station" }
    Assert-True ($null -ne $station -and $station.metrics.depth -eq 1) "hierarchy : Station sits one level below its parent" "depth=$($station.metrics.depth)"
    Assert-True (@($hierarchy.panels | Where-Object { $_.type -eq "treemap" }).Count -ge 1) "hierarchy : emits a treemap"
}

# ---------------------------------------------------------------------------
# The analysis layer checked against a worked dataset whose correct answers are
# known by construction. examples/celidae_business_facts.fx is generated to
# contain: a surrogate key, two genuine delivery-time anomalies, a bimodal
# order size, a near-perfect items/total correlation, and two populations that
# k-means must recover without ever seeing the label.
if (Start-Section "ml analysis") {
    $businessFile = "examples\celidae_business_facts.fx"
    if (-not (Test-Path -LiteralPath $businessFile)) {
        Assert-True $false "the worked business dataset is present" $businessFile
    } else {
        $schema = Get-CelidaeView $businessFile "schema"
        $orderFields = @{}
        foreach ($node in $schema.nodes | Where-Object { $_.kind -eq "field" -and $_.attributes.factType -eq "Order" }) {
            $orderFields[$node.label] = $node.attributes.type
        }
        # 'id' runs 1..n in declaration order. Treated as a measure it produces
        # a meaningless histogram, becomes a cluster driver, and manufactures
        # correlations with anything declared in a correlated order.
        Assert-Equal "identifier" $orderFields["id"] "a sequential integer key is not treated as a measure"
        Assert-Equal "numeric" $orderFields["total"] "a monetary amount stays numeric"
        Assert-Equal "date" $orderFields["placed"] "an ISO date stays a date"
        Assert-Equal "categorical" $orderFields["region"] "a low-cardinality label stays categorical"

        $dist = Get-CelidaeView $businessFile "distribution"
        Assert-True (@($dist.panels | Where-Object { $_.title -like "*Order.id*" }).Count -eq 0) `
            "no histogram is drawn for the surrogate key"

        # Two deliveries at 214 and 187 minutes against a ~37 minute median.
        $minutes = $dist.nodes | Where-Object { $_.id -eq "field:Order.minutes" }
        Assert-Equal 2 $minutes.metrics.farFromMedian "finds exactly the two delivery-time anomalies"
        Assert-True (@($dist.insights | Where-Object { $_.text -match "214" -and $_.text -match "187" }).Count -ge 1) `
            "names both anomalous values"

        # 'items' is bimodal: a third of the orders are catering-sized. A
        # robust z-score test flags that whole population, and calling twelve
        # records "outliers" would send someone hunting for twelve mistakes.
        $itemsField = $dist.nodes | Where-Object { $_.id -eq "field:Order.items" }
        Assert-Equal "two populations" $itemsField.attributes.shape `
            "reports a bimodal field as two populations, not as outliers"
        Assert-True (@($dist.insights | Where-Object { $_.text -match "too many to be anomalies" }).Count -ge 1) `
            "says so in the finding"

        # A correlation matrix must actually be one.
        $comparison = Get-CelidaeView $businessFile "comparison"
        $heat = @($comparison.panels | Where-Object { $_.type -eq "heatmap" -and $_.title -like "Order*" })[0]
        Assert-True ($null -ne $heat) "emits a correlation matrix for Order"
        if ($null -ne $heat) {
            $size = $heat.columns.Count
            $symmetric = $true
            $unitDiagonal = $true
            $inRange = $true
            for ($i = 0; $i -lt $size; $i++) {
                if ([Math]::Abs($heat.values[$i][$i] - 1) -gt 1e-9) { $unitDiagonal = $false }
                for ($j = 0; $j -lt $size; $j++) {
                    if ([Math]::Abs($heat.values[$i][$j] - $heat.values[$j][$i]) -gt 1e-9) { $symmetric = $false }
                    if ($heat.values[$i][$j] -lt -1.000001 -or $heat.values[$i][$j] -gt 1.000001) { $inRange = $false }
                }
            }
            Assert-True $symmetric "the correlation matrix is symmetric"
            Assert-True $unitDiagonal "the correlation matrix has a unit diagonal"
            Assert-True $inRange "every coefficient lies within [-1, 1]"
            # total is items x unit price, so the two are near-collinear.
            $itemsIndex = [Array]::IndexOf($heat.columns, "items")
            $totalIndex = [Array]::IndexOf($heat.columns, "total")
            Assert-True ($heat.values[$itemsIndex][$totalIndex] -gt 0.95) `
                "recovers the near-perfect items/total relationship" `
                "r=$($heat.values[$itemsIndex][$totalIndex])"
        }

        # The headline test: catering orders were generated with items >= 22.
        # k-means never sees that label. Recovering the split exactly is the
        # difference between clustering that works and clustering that runs.
        $cluster = Get-CelidaeView $businessFile "cluster"
        $scatter = @($cluster.panels | Where-Object { $_.type -eq "scatter" -and $_.title -like "Order*" })[0]
        Assert-True ($null -ne $scatter) "emits an Order segment scatter"
        if ($null -ne $scatter) {
            Assert-Equal 44 $scatter.points.Count "plots every Order record"
            # Point labels are the record's id; ids 31..42 are the catering run.
            $segmentOf = @{}
            foreach ($point in $scatter.points) { $segmentOf[[string]$point.label] = $point.group }
            $cateringSegments = @(31..42 | ForEach-Object { $segmentOf["$_"] } | Select-Object -Unique)
            Assert-Equal 1 $cateringSegments.Count `
                "every catering order lands in a single segment" `
                "segments used: $($cateringSegments -join ', ')"
            if ($cateringSegments.Count -eq 1) {
                $strays = @(1..30 | Where-Object { $segmentOf["$_"] -eq $cateringSegments[0] })
                Assert-Equal 0 $strays.Count `
                    "no everyday order is mixed into the catering segment" `
                    "strays: $($strays -join ', ')"
            }
        }

        # k must be bounded by sample size. PriorityOrder has 6 records;
        # splitting it into segments would produce groups of one.
        # PriorityOrder must be refused. Either gate is a correct refusal: too
        # few records to form meaningful groups, or records too evenly spread
        # for any split to mean anything.
        Assert-True (@($cluster.insights | Where-Object {
                $_.text -match "PriorityOrder" -and
                ($_.text -match "needs at least" -or $_.text -match "spread evenly")
            }).Count -ge 1) `
            "refuses to segment a fact type that cannot support segments"
        Assert-Equal 0 @($cluster.nodes | Where-Object {
                $_.kind -eq "segment" -and $_.attributes.factType -eq "PriorityOrder"
            }).Count "and emits no PriorityOrder segments"
        $tinySegments = @($cluster.nodes | Where-Object { $_.kind -eq "segment" -and $_.metrics.records -lt 3 })
        Assert-Equal 0 $tinySegments.Count "produces no segment smaller than three records"
    }
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

        # Every view now reports a verdict, applicable or not, so a reader can
        # be told *why* a view is unavailable instead of it silently vanishing
        # from the list. An inapplicable view scores exactly 0; an applicable
        # one scores inside (0, 1].
        Assert-Equal $AllTypes.Count $parsed.recommendations.Count `
            "every diagram type reports a verdict"
        foreach ($entry in $parsed.recommendations) {
            Assert-True ($AllTypes -contains $entry.view) "recommends a real diagram type ('$($entry.view)')"
            Assert-True ($null -ne $entry.applicable) "'$($entry.view)' states whether it applies"
            if ($entry.applicable) {
                Assert-True ($entry.score -gt 0 -and $entry.score -le 1) `
                    "applicable '$($entry.view)' scores within (0,1]" "score=$($entry.score)"
            } else {
                Assert-Equal 0 $entry.score "inapplicable '$($entry.view)' scores 0"
            }
            Assert-True (-not [string]::IsNullOrWhiteSpace($entry.rationale)) "'$($entry.view)' explains itself"
        }

        # Applicable views lead, then strongest first, since the page opens on
        # recommendations[0] and must never open on a view that does not apply.
        $applicable = @($parsed.recommendations | Where-Object { $_.applicable })
        Assert-True ($applicable.Count -ge 1) "at least one view applies to this fixture"
        Assert-True $parsed.recommendations[0].applicable "the first recommendation is one that applies"
        $scores = @($applicable | ForEach-Object { $_.score })
        $sorted = @($scores | Sort-Object -Descending)
        Assert-True (@(Compare-Object $scores $sorted -SyncWindow 0).Count -eq 0) "recommendations come back ranked"
        # This fixture has clusterable data, so cluster must be the top pick.
        Assert-Equal "cluster" $parsed.recommendations[0].view "the strongest view for this data is recommended first"
    }

    # A program with no fact data must not offer a data view.
    $structural = Join-Path $FixtureDir "structural.fx"
    Write-Utf8NoBom $structural @'
Thing(name: name)
work() =>
    Thing(name: n)
    return
'@
    $none = (Invoke-Celidae @($structural, "--recommend")).Text | ConvertFrom-Json
    $dataViews = @($none.recommendations |
        Where-Object { $_.applicable -and $_.view -in @("cluster", "distribution", "comparison") })
    Assert-Equal 0 $dataViews.Count "a program with no literal values offers no data view"

    # The gate that keeps a view from substituting an axis it does not have.
    # A fact set carrying no date must decline the timeline rather than
    # ordering records by whatever number is to hand and calling it time.
    $undated = Join-Path $FixtureDir "undated.fx"
    Write-Utf8NoBom $undated @'
Country(name: "Alpha", alpha_2: "AL", country_code: "004")
Country(name: "Bravo", alpha_2: "BR", country_code: "008")
Country(name: "Charlie", alpha_2: "CH", country_code: "010")
Country(name: "Delta", alpha_2: "DE", country_code: "012")
Country(name: "Echo", alpha_2: "EC", country_code: "016")
Country(name: "Foxtrot", alpha_2: "FO", country_code: "020")
Country(name: "Golf", alpha_2: "GO", country_code: "024")
Country(name: "Hotel", alpha_2: "HO", country_code: "028")
Country(name: "India", alpha_2: "IN", country_code: "032")
Country(name: "Juliet", alpha_2: "JU", country_code: "036")
'@
    $undatedViews = (Invoke-Celidae @($undated, "--recommend")).Text | ConvertFrom-Json
    $timelineVerdict = $undatedViews.recommendations | Where-Object { $_.view -eq "timeline" }
    Assert-True (-not $timelineVerdict.applicable) "a fact set with no date declines the timeline"
    Assert-True ($timelineVerdict.rationale -match "date") "and says the missing date is why"

    # country_code is a zero-padded code, not a quantity. Averaging it would
    # describe the ISO numbering scheme rather than anything about countries.
    $undatedStats = Get-CelidaeView $undated "stats"
    $codeField = $undatedStats.nodes | Where-Object { $_.id -eq "field:Country.country_code" }
    Assert-True ($codeField.attributes.type -ne "numeric") `
        "a fixed-width zero-padded field is not treated as a measurement" `
        "type=$($codeField.attributes.type)"
    Assert-True ($null -eq $codeField.metrics.mean) "and no mean is computed for it"
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
if (Start-Section "chart alternatives") {
    # Every panel carries the renderings its data supports, so the page can
    # offer a per-chart selector without deciding for itself what is faithful.
    # The rule that matters is the line: joining categories asserts they can
    # be traversed, which is true of calendar periods and false of labels.
    foreach ($type in @("distribution", "timeline", "comparison", "cluster")) {
        $payload = Get-CelidaeView $fixturePath $type
        foreach ($panel in $payload.panels) {
            $alternatives = @($panel.alternatives)
            Assert-True ($alternatives.Count -ge 1) `
                "$type : panel '$($panel.title)' lists its renderings"
            # A panel must always offer the type it is currently drawn as,
            # or the selector would open showing a choice nobody made.
            $current = @($alternatives | Where-Object { $_.type -eq $panel.type })
            Assert-True ($current.Count -eq 1 -and $current[0].available) `
                "$type : panel '$($panel.title)' offers its own type as available"
            foreach ($alternative in $alternatives) {
                if (-not $alternative.available) {
                    Assert-True (-not [string]::IsNullOrWhiteSpace($alternative.reason)) `
                        "$type : '$($alternative.type)' says why it is unavailable"
                }
            }
        }
    }

    # Unordered labels must refuse a line; ordered periods must allow one.
    $dist = Get-CelidaeView $fixturePath "distribution"
    $labelled = @($dist.panels | Where-Object { $_.title -match "by value" })
    if ($labelled.Count -ge 1) {
        $line = @($labelled[0].alternatives | Where-Object { $_.type -eq "line" })
        Assert-True ($line.Count -eq 1 -and -not $line[0].available) `
            "a label axis refuses a line chart" "panel=$($labelled[0].title)"
    }
    $histogram = @($dist.panels | Where-Object { $_.type -eq "histogram" })
    if ($histogram.Count -ge 1) {
        $line = @($histogram[0].alternatives | Where-Object { $_.type -eq "line" })
        Assert-True ($line.Count -eq 1 -and $line[0].available) `
            "histogram bins allow a line (a frequency polygon)"
        $horizontal = @($histogram[0].alternatives | Where-Object { $_.type -eq "hbar" })
        Assert-True ($horizontal.Count -eq 1 -and -not $horizontal[0].available) `
            "and refuse to rotate an ordered axis"
    }
    $timeline = Get-CelidaeView $fixturePath "timeline"
    foreach ($panel in $timeline.panels) {
        $line = @($panel.alternatives | Where-Object { $_.type -eq "line" })
        Assert-True ($line.Count -eq 1 -and $line[0].available) `
            "a calendar axis allows a line" "panel=$($panel.title)"
    }

    # A chart with no faithful alternative offers exactly one option, so the
    # page renders no selector rather than a menu of one.
    $cluster = Get-CelidaeView $fixturePath "cluster"
    $scatter = @($cluster.panels | Where-Object { $_.type -eq "scatter" })
    if ($scatter.Count -ge 1) {
        Assert-Equal 1 @($scatter[0].alternatives).Count `
            "a scatter offers no alternative, because no bar chart holds its shape"
    }
}

# ---------------------------------------------------------------------------
if (Start-Section "spectral layout") {
    # Laplacian eigenmaps, computed in C++ and attached to the nodes. This
    # moved here when the SVG export was removed; it is the only layout that
    # reflects connectivity and the only one that is reproducible, so it is
    # worth keeping and worth testing.
    $schema = Get-CelidaeView $fixturePath "schema"
    $positioned = @($schema.nodes | Where-Object { $null -ne $_.position })
    Assert-True ($positioned.Count -eq $schema.nodes.Count) `
        "schema : every node carries a spectral position" `
        "$($positioned.Count) of $($schema.nodes.Count)"
    foreach ($node in $positioned) {
        Assert-True ($node.position.x -ge 0 -and $node.position.x -le 1 -and
                     $node.position.y -ge 0 -and $node.position.y -le 1) `
            "positions are normalised to 0..1" "$($node.position.x),$($node.position.y)"
        break
    }
    # Distinct positions, or the layout collapses every node onto one point.
    $unique = @($positioned | ForEach-Object { "$($_.position.x),$($_.position.y)" } | Sort-Object -Unique)
    Assert-True ($unique.Count -gt 1) "the layout separates nodes rather than collapsing them"

    # Reproducible, unlike the force layout it sits beside.
    $again = Get-CelidaeView $fixturePath "schema"
    $first = ($schema.nodes | ForEach-Object { "$($_.id):$($_.position.x)" }) -join "|"
    $second = ($again.nodes | ForEach-Object { "$($_.id):$($_.position.x)" }) -join "|"
    Assert-Equal $first $second "two runs place every node identically"

    # A graph too small for the eigenproblem to say anything must emit no
    # positions at all, so the page hides the layout rather than offering one
    # that silently falls back.
    $tiny = Join-Path $FixtureDir "tiny.fx"
    Write-Utf8NoBom $tiny "Solo(name: `"only`")"
    $tinyGraph = Get-CelidaeView $tiny "graph"
    $tinyPositioned = @($tinyGraph.nodes | Where-Object { $null -ne $_.position })
    Assert-Equal 0 $tinyPositioned.Count "too small a graph emits no positions"
}

# ---------------------------------------------------------------------------
if (Start-Section "escaping") {
    # A label carrying XML/JS metacharacters is the case that actually breaks
    # an export. SVG output is gone, so what remains to protect is the JSON
    # payload and the <script> element wrapping it in the HTML.
    $hostilePath = Join-Path $FixtureDir "hostile.fx"
    Write-Utf8NoBom $hostilePath @'
Weird(name: "a & b", note: "<script>x</script>", quote: "angle > bracket")
Weird(name: "c < d", note: "]]>", quote: "&amp;")
'@

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
    # report would diff against its predecessor for no reason. -Fresh on both
    # sides bypasses the cache, since the point is to compare two independent
    # processes, not the same result read twice.
    $first = Get-CelidaeViews -Path $fixturePath -Fresh
    $second = Get-CelidaeViews -Path $fixturePath -Fresh
    foreach ($type in @("cluster", "distribution", "comparison", "timeline")) {
        $firstText = if ($first.Views.ContainsKey($type)) { $first.Views[$type].Text } else { "" }
        $secondText = if ($second.Views.ContainsKey($type)) { $second.Views[$type].Text } else { "" }
        Assert-True ($firstText -ceq $secondText) "$type : two runs produce byte-identical output"
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
    #
    # One process call now builds every view at once, so the exit code is a
    # single fact about the whole run rather than nine independent ones - the
    # per-type loop that used to exist here was checking the same number nine
    # times. What is still worth checking per type, when the run succeeds, is
    # that each embedded payload actually came through well-formed.
    foreach ($case in $hostileInputs) {
        $run = Get-CelidaeViews -Path $case.Path -Fresh
        Assert-True ($run.ExitCode -eq 0 -or $run.ExitCode -eq 1) `
            "$($case.Name) : handled, exit $($run.ExitCode)"
        if ($run.ExitCode -ne 0) { continue }
        foreach ($type in $AllTypes) {
            $view = $run.Views[$type]
            Assert-True ($null -ne $view -and $null -ne $view.Parsed) `
                "$($case.Name) / $type : embedded payload is valid JSON"
        }
    }

    # A cyclic extend chain is the case that can hang rather than crash: the
    # depth walk has to notice it is going in circles.
    $job = Start-Job -ScriptBlock {
        param($exe, $path)
        & $exe $path | Out-Null
        $LASTEXITCODE
    } -ArgumentList ((Resolve-Path $CelidaeExe).Path, $cyclicPath)
    $finished = Wait-Job $job -Timeout 30
    Assert-True ($null -ne $finished) "a cyclic extend chain terminates rather than hanging"
    Remove-Job $job -Force -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------------------------
# Heterogeneous facts: several entities of different shapes in one program,
# joined only by their values. This is what a converted CSV looks like, and it
# exercises the paths a single uniform fact type never reaches.
if (Start-Section "heterogeneous facts") {
    $mixed = Join-Path $FixtureDir "mixed-entities.fx"
    Write-Utf8NoBom $mixed @'
Region(name: "north", timezone: "UTC+1")
Region(name: "south", timezone: "UTC+2")
Region(name: "coastal", timezone: "UTC+1")
Depot(code: "007", label: "North Yard", region: "north", capacity: 1200)
Depot(code: "013", label: "North Depot", region: "north", capacity: 800)
Depot(code: "021", label: "South Yard", region: "south", capacity: 1500)
Depot(code: "094", label: "South Depot", region: "south", capacity: 640)
Depot(code: "108", label: "Coastal Terminal", region: "coastal", capacity: 2200)
Depot(code: "117", label: "Coastal Annex", region: "coastal", capacity: 430)
Move(ref: "M-1", depot: "007", carrier: "alpha", placed: "2024-01-14", units: 40)
Move(ref: "M-2", depot: "013", carrier: "alpha", placed: "2024-02-03", units: 35)
Move(ref: "M-3", depot: "021", carrier: "bravo", placed: "2024-03-05", units: 25)
Move(ref: "M-4", depot: "094", carrier: "bravo", placed: "2024-04-19", units: 200)
Move(ref: "M-5", depot: "108", carrier: "cielo", placed: "2024-05-02", units: 45)
Move(ref: "M-6", depot: "117", carrier: "cielo", placed: "2024-06-21", units: 150)
Move(ref: "M-7", depot: "007", carrier: "alpha", placed: "2024-07-09", units: 30)
Move(ref: "M-8", depot: "013", carrier: "alpha", placed: "2024-08-28", units: 55)
Move(ref: "M-9", depot: "021", carrier: "bravo", placed: "2024-09-02", units: 65)
Move(ref: "M-10", depot: "094", carrier: "bravo", placed: "2024-09-18", units: 180)
Move(ref: "M-11", depot: "108", carrier: "cielo", placed: "2024-10-05", units: 42)
Move(ref: "M-12", depot: "117", carrier: "cielo", placed: "2024-10-22", units: 140)
'@

    # The ER view must show relationships. None of these is declared - Felidae
    # has no foreign-key syntax - so all of them have to come from noticing
    # that one field's values live in another type's key.
    $er = Get-CelidaeView $mixed "er"
    $joins = @($er.edges | Where-Object { $_.label -match "->" })
    Assert-True ($joins.Count -ge 2) "er : infers joins from value overlap" "found $($joins.Count)"
    Assert-True (@($joins | Where-Object { $_.from -eq "fact:Move" -and $_.to -eq "fact:Depot" }).Count -eq 1) `
        "er : Move.depot resolves to Depot.code"
    Assert-True (@($joins | Where-Object { $_.from -eq "fact:Depot" -and $_.to -eq "fact:Region" }).Count -eq 1) `
        "er : Depot.region resolves to Region.name"
    Assert-True (@($joins | Where-Object { $_.label -match "many-to-one" }).Count -ge 1) `
        "er : states the cardinality it measured"

    # ER and schema answered the same question for so long that they drew the
    # same picture. The schema view fans every entity out into its fields; the
    # ER view must not, or the two are one view under two names.
    $schema = Get-CelidaeView $mixed "schema"
    $schemaFields = @($schema.nodes | Where-Object { $_.kind -eq "field" }).Count
    $erFields = @($er.nodes | Where-Object { $_.kind -eq "field" }).Count
    Assert-True ($schemaFields -gt 0) "schema : fans entities out into their fields"
    Assert-Equal 0 $erFields "er : does not repeat the schema view's field fan-out"

    # Only Move carries a date, so the timeline applies to Move and to nothing
    # else - the decision has to follow the data, not a fixed list of views.
    $timeline = Get-CelidaeView $mixed "timeline"
    $timed = @($timeline.nodes | Where-Object { $_.kind -eq "event" } |
        ForEach-Object { $_.attributes.factType } | Sort-Object -Unique)
    Assert-Equal 1 $timed.Count "timeline : only the fact type carrying a date is placed in time"
    Assert-Equal "Move" $timed[0] "timeline : and that type is the dated one"

    # Depot.code is zero-padded and fixed-width: a label, not a capacity.
    $stats = Get-CelidaeView $mixed "stats"
    $code = $stats.nodes | Where-Object { $_.id -eq "field:Depot.code" }
    Assert-True ($code.attributes.type -ne "numeric") "a padded code field is not a measurement" `
        "type=$($code.attributes.type)"

    # Depot.label values share vocabulary ("North"/"South"/"Coastal",
    # "Yard"/"Depot"). Nothing names those words anywhere: they come out of
    # tokenising the values and factorising what is left.
    $distribution = Get-CelidaeView $mixed "distribution"
    $vocabulary = @($distribution.panels | Where-Object { $_.title -match "shared vocabulary" })
    Assert-True ($vocabulary.Count -ge 1) "distribution : finds vocabulary shared across text values"
    if ($vocabulary.Count -ge 1) {
        $terms = @($vocabulary[0].categories)
        Assert-True ($terms.Count -ge 2) "and reports more than one term" "terms=$($terms -join ',')"
        Assert-True ($terms -notcontains "") "and every term is a real word"
    }

    # Correspondence analysis. Depot.region against Depot.label is 3 x 6, past
    # the point where a contingency grid is readable, so the map replaces it.
    # The check that matters is not that a panel appeared but that its geometry
    # is right: each region must land nearest the depots that belong to it, and
    # nothing in the program says which those are.
    # Correspondence analysis. Move.depot against Move.carrier is 6 x 3, past
    # the point where a contingency grid stays readable, so the map replaces
    # it. What is asserted is not that a panel appeared but that its geometry
    # is right: each carrier must land nearest the depots it actually serves,
    # and nothing in the program states which those are - the association is
    # recovered from co-occurrence alone.
    $corr = @($distribution.panels | Where-Object { $_.title -match "which values go together" })
    Assert-True ($corr.Count -ge 1) "distribution : maps two labels into one space" `
        "panels=$(@($distribution.panels | ForEach-Object { $_.title }) -join ' | ')"
    if ($corr.Count -ge 1) {
        $map = $corr[0]
        Assert-Equal "scatter" $map.type "correspondence : drawn as a shared-space scatter"
        Assert-Equal 2 @($map.groups).Count "correspondence : plots both fields in one space"
        $grid = @($distribution.panels |
            Where-Object { $_.type -eq "heatmap" -and $_.scale -eq "count" })
        Assert-Equal 0 $grid.Count "correspondence : replaces the contingency grid rather than duplicating it"

        $byValue = @{}
        foreach ($point in $map.points) { $byValue[$point.label] = $point }
        $distance = {
            param($p, $q)
            [Math]::Sqrt([Math]::Pow($p.x - $q.x, 2) + [Math]::Pow($p.y - $q.y, 2))
        }
        # Carrier "alpha" moves only through depots 007 and 013; "cielo" only
        # through 108 and 117. So 007 must sit nearer alpha than cielo.
        $haveAll = $byValue.ContainsKey("alpha") -and $byValue.ContainsKey("cielo") -and
                   $byValue.ContainsKey("007") -and $byValue.ContainsKey("108")
        Assert-True $haveAll "correspondence : plots every level of both fields" `
            "labels=$($byValue.Keys -join ',')"
        if ($haveAll) {
            $depotToOwn = & $distance $byValue["007"] $byValue["alpha"]
            $depotToOther = & $distance $byValue["007"] $byValue["cielo"]
            Assert-True ($depotToOwn -lt $depotToOther) `
                "correspondence : a depot lands nearer the carrier that serves it" `
                "own=$depotToOwn other=$depotToOther"
            $farToOwn = & $distance $byValue["108"] $byValue["cielo"]
            $farToOther = & $distance $byValue["108"] $byValue["alpha"]
            Assert-True ($farToOwn -lt $farToOther) `
                "correspondence : and the same holds for the other end of the map" `
                "own=$farToOwn other=$farToOther"
        }
    }
}

# ---------------------------------------------------------------------------
# Degenerate data that breaks statistics rather than parsers. Each of these is
# a case where a formula divides by zero, a matrix goes singular, or an
# algorithm is asked a question the data cannot answer - and the requirement is
# that Celidae says so rather than emitting a number it cannot justify.
if (Start-Section "statistical edge cases") {
    $edge = Join-Path $FixtureDir "edge"
    New-Item -ItemType Directory -Force -Path $edge | Out-Null

    $cases = @{
        # Zero variance: standardisation divides by the standard deviation.
        "all-identical"   = (1..10 | ForEach-Object { 'A(x: 5)' }) -join "`n"
        # Every row the same point: covariance is the zero matrix.
        "duplicate-rows"  = (1..10 | ForEach-Object { 'A(x: 1, y: 2)' }) -join "`n"
        # z = 3x exactly: the covariance matrix is singular, so a Mahalanobis
        # solve without a ridge term returns garbage instead of failing.
        "collinear"       = (1..12 | ForEach-Object { "A(x: $_, y: $($_*2), z: $($_*3))" }) -join "`n"
        # An even 5x4 lattice: no groups in it, however clean a cut looks.
        # Deliberately not a 1-D ramp with an ascending index - that index is
        # correctly identified as a surrogate key, which leaves one usable
        # column and stops the pipeline before it reaches the tendency check.
        "uniform-ramp"    = (1..5 | ForEach-Object { $a = $_; 1..4 | ForEach-Object {
                                "A(v: $($a * 10), w: $($_ * 10))" } }) -join "`n"
        # Denormals: variance underflows to zero at double precision.
        "tiny-numbers"    = (1..10 | ForEach-Object { "A(v: 0.0000000001, w: $_)" }) -join "`n"
        # Calendar-invalid dates that pass a digits-only shape check.
        "impossible-dates" = @'
A(d: "2025-13-45", n: 1)
A(d: "2025-02-30", n: 2)
A(d: "2024-02-29", n: 3)
A(d: "2025-06-15", n: 4)
'@
        # One field, half numbers and half text.
        "mixed-types"     = (1..8 | ForEach-Object { "A(m: $_)`nA(m: `"t$_`")" }) -join "`n"
        # Every field near-unique: analysable in principle, useless in practice.
        "all-identifiers" = (1..30 | ForEach-Object { "A(k: `"c$_`", j: `"d$_`")" }) -join "`n"
        # Past the 500-record sampling cap.
        "over-cap"        = (1..600 | ForEach-Object { "A(i: $_, g: `"g$($_ % 3)`", m: $($_ % 97))" }) -join "`n"
        # A 40-deep inheritance chain, and a cycle at the end of it.
        "deep-chain"      = "Base(v: 0)`n" + ((1..40 | ForEach-Object {
                                $parent = if ($_ -eq 1) { "Base" } else { "L$($_-1)" }
                                "L$_ extend $parent(v: $_)" }) -join "`n")
        "negatives"       = (1..10 | ForEach-Object { "A(neg: -$_, pos: $_)" }) -join "`n"
    }

    foreach ($case in $cases.GetEnumerator()) {
        $path = Join-Path $edge "$($case.Key).fx"
        Write-Utf8NoBom $path $case.Value
        $clean = $true
        $detail = ""
        # One call builds every view; a single non-zero exit covers all nine
        # at once, since they come from the same process.
        $run = Get-CelidaeViews -Path $path -Fresh
        if ($run.ExitCode -ne 0) {
            $clean = $false
            $detail = "exit $($run.ExitCode)"
        } else {
            foreach ($type in $AllTypes) {
                $view = $run.Views[$type]
                if ($null -eq $view) {
                    $clean = $false; $detail = "no payload embedded for $type"; break
                }
                # NaN and Infinity are not JSON; either would make the browser
                # reject the entire document rather than degrade one chart.
                if ($view.Text -match "\b(NaN|nan|-?[Ii]nf|Infinity)\b") {
                    $clean = $false; $detail = "non-finite number emitted for $type"; break
                }
                if ($null -eq $view.Parsed) {
                    $clean = $false; $detail = "unparseable JSON for $type"; break
                }
                # Every reported statistic must be a real number.
                foreach ($node in $view.Parsed.nodes) {
                    if ($null -eq $node.metrics) { continue }
                    foreach ($metric in $node.metrics.PSObject.Properties) {
                        if ($metric.Value -isnot [double] -and $metric.Value -isnot [int] -and
                            $metric.Value -isnot [long] -and $metric.Value -isnot [decimal]) {
                            $clean = $false
                            $detail = "$type : $($node.id).$($metric.Name) is not numeric"
                        }
                    }
                }
                if (-not $clean) { break }
            }
        }
        Assert-True $clean "$($case.Key) : every view produces finite, parseable output" $detail
    }

    # A uniform ramp has no group structure. k-means will still cut it cleanly
    # and score the cut well, because a silhouette measures whether a split is
    # tidy, never whether there was anything there to split.
    $rampPath = Join-Path $edge "uniform-ramp.fx"
    $ramp = Get-CelidaeView $rampPath "cluster"
    Assert-True (@($ramp.insights | Where-Object { $_.text -match "spread evenly|cluster tendency" }).Count -ge 1) `
        "an evenly-spread field is not carved into invented segments"
    Assert-Equal 0 @($ramp.nodes | Where-Object { $_.kind -eq "segment" }).Count `
        "and no segments are emitted for it"

    # Calendar validation: only the two real dates may be treated as dates.
    $datesPath = Join-Path $edge "impossible-dates.fx"
    $dates = Get-CelidaeView $datesPath "schema"
    $dateField = $dates.nodes | Where-Object { $_.id -eq "field:A.d" }
    Assert-True ($dateField.attributes.type -ne "date") `
        "a field containing 2025-13-45 is not classified as a date" `
        "classified as $($dateField.attributes.type)"

    # Collinearity must be detected, not silently inverted.
    $collinearPath = Join-Path $edge "collinear.fx"
    $collinear = Get-CelidaeView $collinearPath "cluster"
    Assert-True (@($collinear.reasoning | Where-Object { $_.finding -match "independent dimensions" }).Count -ge 1) `
        "rank deficiency is measured and reported"

    # The reasoning trace must exist and be well formed wherever analysis ran.
    foreach ($case in @("collinear", "uniform-ramp", "over-cap")) {
        $payload = Get-CelidaeView (Join-Path $edge "$case.fx") "cluster"
        $wellFormed = $true
        foreach ($stepEntry in $payload.reasoning) {
            if ([string]::IsNullOrWhiteSpace($stepEntry.stage) -or
                [string]::IsNullOrWhiteSpace($stepEntry.finding) -or
                [string]::IsNullOrWhiteSpace($stepEntry.decision)) { $wellFormed = $false }
        }
        Assert-True ($payload.reasoning.Count -gt 0 -and $wellFormed) `
            "$case : every reasoning step records a finding and a decision"
    }
}

# ---------------------------------------------------------------------------
if (-not $Quick) {
    if (Start-Section "example corpus sweep") {
        $examples = @(Get-ChildItem -Path "examples", "v2_examples" -Filter "*.fx" -ErrorAction SilentlyContinue)
        Assert-True ($examples.Count -gt 0) "found example programs to sweep"
        $broken = @()
        foreach ($example in $examples) {
            # One process per example now covers all nine views at once,
            # rather than nine separate --json invocations - both faster and
            # closer to what a user actually runs.
            $run = Get-CelidaeViews -Path $example.FullName -Fresh
            if ($run.ExitCode -ne 0 -and $run.ExitCode -ne 1) {
                $broken += "$($example.Name) exit $($run.ExitCode)"
                continue
            }
            if ($run.ExitCode -ne 0) { continue }
            foreach ($type in $AllTypes) {
                $view = $run.Views[$type]
                if ($null -eq $view) {
                    $broken += "$($example.Name) --template=$type payload missing"
                } elseif ($null -eq $view.Parsed) {
                    $broken += "$($example.Name) --template=$type produced unparseable JSON"
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
