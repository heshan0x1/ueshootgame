$ErrorActionPreference = 'Stop'

$root = "D:\workspace\ShootGame\UnrealEngine\Engine\Source\Runtime\Net\Iris"
$allFiles = Get-ChildItem -Path $root -Recurse -Include *.cpp,*.h,*.inl

$badEncoding = @()
$unbalanced = @()
$emptyFiles = @()
$count = 0

$openBrace = [char]0x7B
$closeBrace = [char]0x7D

foreach ($f in $allFiles) {
    $count++
    $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
    if ($bytes.Length -eq 0) {
        $emptyFiles += $f.FullName
        continue
    }
    
    try {
        $utf8 = New-Object System.Text.UTF8Encoding $false, $true
        $content = $utf8.GetString($bytes)
    } catch {
        $badEncoding += $f.FullName
        continue
    }
    
    # 剥离字符串字面量
    $stripped = [System.Text.RegularExpressions.Regex]::Replace($content, '"([^"\\]|\\.)*"', '""')
    # 剥离字符字面量
    $stripped = [System.Text.RegularExpressions.Regex]::Replace($stripped, "'([^'\\]|\\.)*'", "''")
    # 剥离块注释
    $stripped = [System.Text.RegularExpressions.Regex]::Replace($stripped, '/\*[\s\S]*?\*/', '')
    # 剥离行注释
    $stripped = [System.Text.RegularExpressions.Regex]::Replace($stripped, '//[^\r\n]*', '')
    
    $openCount = 0
    $closeCount = 0
    foreach ($c in $stripped.ToCharArray()) {
        if ($c -eq $openBrace) { $openCount++ }
        elseif ($c -eq $closeBrace) { $closeCount++ }
    }
    
    if ($openCount -ne $closeCount) {
        $unbalanced += "$($f.FullName) [open=$openCount close=$closeCount diff=$($openCount - $closeCount)]"
    }
}

Write-Output "Total files scanned: $count"
Write-Output "UTF-8 decode failures: $($badEncoding.Count)"
foreach ($e in $badEncoding) { Write-Output "  $e" }
Write-Output "Empty files: $($emptyFiles.Count)"
foreach ($e in $emptyFiles) { Write-Output "  $e" }
Write-Output "Brace unbalanced: $($unbalanced.Count)"
foreach ($u in $unbalanced) { Write-Output "  $u" }