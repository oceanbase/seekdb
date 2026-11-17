$archive = Get-Item dist/seekdb-binary-*.zip
$destination = Join-Path $env:RUNNER_TEMP 'seekdb-package-check'
Expand-Archive -LiteralPath $archive.FullName -DestinationPath $destination
$executable = Get-ChildItem $destination -Filter seekdb.exe -Recurse
& $executable.FullName --version
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
