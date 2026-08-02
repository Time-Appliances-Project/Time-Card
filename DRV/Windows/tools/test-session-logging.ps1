param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
$controlCenter = Join-Path $root "TimeCardControlCenter"
$modelPath = Join-Path $controlCenter "SessionLogging.cs"
$windowPath = Join-Path $controlCenter "MainWindow.xaml.cs"
$xamlPath = Join-Path $controlCenter "MainWindow.xaml"
$projectPath = Join-Path $controlCenter "TimeCardControlCenter.csproj"
$productPath = Join-Path $controlCenter "MainWindow.Product.cs"

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "Session logging test failed: $Message"
    }
}

$modelSource = Get-Content -LiteralPath $modelPath -Raw
Add-Type -TypeDefinition $modelSource -Language CSharp

$severityType = [TimeCardControlCenter.SessionLogSeverity]
$store = New-Object TimeCardControlCenter.SessionLogStore(3)
$baseTime = [DateTime]::SpecifyKind(
    [DateTime]::ParseExact("2026-08-01 12:00:00", "yyyy-MM-dd HH:mm:ss",
        [Globalization.CultureInfo]::InvariantCulture),
    [DateTimeKind]::Utc)

$store.Append($baseTime, $severityType::Information, "Connection",
    "TC-001", "Connected to TimeCard0.") | Out-Null
$store.Append($baseTime.AddSeconds(1), $severityType::Trace, "Clock",
    "TC-001", "Clock telemetry refreshed.") | Out-Null
$store.Append($baseTime.AddSeconds(2), $severityType::Warning, "Serial & GNSS",
    "TC-001", "Secondary receiver not present.") | Out-Null
$store.Append($baseTime.AddSeconds(3), $severityType::Error, "I2C & Sensors",
    "TC-001", "Sensor read failed: timeout.`r`nRetry available.") | Out-Null

Assert-True ($store.Capacity -eq 3) "capacity was not retained"
Assert-True ($store.Count -eq 3) "the bounded store did not retain three records"
Assert-True ($store.DroppedRecordCount -eq 1) "discarded records were not counted"
$all = $store.Query($null)
Assert-True ($all[0].Message -eq "Clock telemetry refreshed.") `
    "the oldest record was not discarded"
Assert-True ($all[2].Message -notmatch "`r|`n") `
    "multi-line messages were not normalized to one structured record"

$filter = New-Object TimeCardControlCenter.SessionLogFilter
$filter.Severity = $severityType::Error
$filter.Category = "I2C & Sensors"
$filter.SearchText = "retry"
$filtered = $store.Query($filter)
Assert-True ($filtered.Count -eq 1) "combined severity/category/search filtering failed"
Assert-True ($filtered[0].CardContext -eq "TC-001") "card context was not retained"
Assert-True ($filtered[0].TimestampUtc.Kind -eq [DateTimeKind]::Utc) `
    "timestamps were not normalized to UTC"

Assert-True ([TimeCardControlCenter.SessionLogStore]::InferSeverity(
    "I2C read failed: timeout") -eq $severityType::Error) `
    "error severity inference failed"
Assert-True ([TimeCardControlCenter.SessionLogStore]::InferSeverity(
    "Secondary GNSS not present") -eq $severityType::Warning) `
    "warning severity inference failed"
Assert-True ([TimeCardControlCenter.SessionLogStore]::InferSeverity(
    "Clock telemetry refreshed") -eq $severityType::Trace) `
    "trace severity inference failed"
Assert-True ([TimeCardControlCenter.SessionLogStore]::InferCategory(
    "SA53 holdover refreshed") -eq "Atomic clock") `
    "atomic-clock category inference failed"
Assert-True ([TimeCardControlCenter.SessionLogStore]::InferCategory(
    "u-blox GNSS receiver ready") -eq "Serial & GNSS") `
    "GNSS category inference failed"

$text = $store.ToText($filter)
Assert-True ($text -match "2026-08-01T12:00:03.000Z \[ERROR\]") `
    "text export does not contain UTC and severity"
Assert-True ($text -match "\[I2C & Sensors\] \[TC-001\]") `
    "text export does not contain category and card context"

$json = $store.ToJson($filter) | ConvertFrom-Json
Assert-True ($json.schemaVersion -eq 1) "JSON schema version is missing"
Assert-True ($json.capacity -eq 3) "JSON capacity metadata is incorrect"
Assert-True ($json.retainedRecordCount -eq 3) "JSON retained count is incorrect"
Assert-True ($json.exportedRecordCount -eq 1) "JSON filtered count is incorrect"
Assert-True ($json.droppedRecordCount -eq 1) "JSON discarded count is incorrect"
Assert-True ($json.records[0].severity -eq "Error") "JSON severity is incorrect"
Assert-True ($json.records[0].category -eq "I2C & Sensors") `
    "JSON category is incorrect"
Assert-True ($json.records[0].cardContext -eq "TC-001") `
    "JSON card context is incorrect"

[xml](Get-Content -LiteralPath $xamlPath -Raw) | Out-Null
$xaml = Get-Content -LiteralPath $xamlPath -Raw
$window = Get-Content -LiteralPath $windowPath -Raw
$project = Get-Content -LiteralPath $projectPath -Raw
$product = Get-Content -LiteralPath $productPath -Raw

Assert-True ($xaml -match 'x:Name="SessionLogListBox"') `
    "the structured session-log list is missing"
Assert-True ($xaml -match 'x:Name="SessionLogSeverityCombo"') `
    "the severity filter is missing"
Assert-True ($xaml -match 'x:Name="SessionLogCategoryCombo"') `
    "the category filter is missing"
Assert-True ($xaml -match 'x:Name="SessionLogSearchTextBox"') `
    "the search control is missing"
Assert-True ($xaml -match 'Click="SessionLogExport_Click"') `
    "the direct export action is missing"
Assert-True ($xaml -match 'x:Name="LogTextBox" Visibility="Collapsed"') `
    "the support-bundle compatibility text view is missing"
Assert-True ($window -match 'private void Log\(string message\)') `
    "the existing Log(string) surface was not preserved"
Assert-True ($window -match 'sessionLogStore\.Append\(DateTime\.UtcNow') `
    "Log(string) is not backed by structured records"
Assert-True ($window -match 'sessionLogStore\.ToJson\(filter\)') `
    "JSON export is not wired to the session log"
Assert-True ($project -match '<Compile Include="SessionLogging\.cs"') `
    "SessionLogging.cs is not compiled by the project"
Assert-True ($product -match 'session-log\.txt"\s*,\s*LogTextBox\.Text') `
    "support bundles no longer consume the complete session log"

$store.Clear()
Assert-True ($store.Count -eq 0) "clear did not remove retained records"
Assert-True ($store.DroppedRecordCount -eq 0) "clear did not reset discarded count"

Write-Host "Session logging tests passed: bounded retention, structured fields, filters, TXT/JSON export, and support-bundle compatibility."
