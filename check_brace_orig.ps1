$ErrorActionPreference = 'Stop'

$openBrace = [char]0x7B
$closeBrace = [char]0x7D

function Get-BraceCount {
    param([string]$content)
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
    return @($openCount, $closeCount)
}

$files = @(
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/ReplicationSystem/Filtering/NetObjectGroups.cpp',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/ReplicationSystem/Filtering/ObjectScopeHysteresisUpdater.cpp',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/ReplicationSystem/Filtering/ReplicationFiltering.h',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/Stats/NetStats.cpp'
)

foreach ($f in $files) {
    $original = git -C D:\workspace\ShootGame show "HEAD:$f" 2>$null
    if ($original) { $original = [string]::Join("`n", $original) }
    $currentPath = "D:\workspace\ShootGame\$($f -replace '/','\')"
    $current = [System.IO.File]::ReadAllText($currentPath, [System.Text.Encoding]::UTF8)
    
    $origCounts = Get-BraceCount $original
    $currCounts = Get-BraceCount $current
    
    Write-Output "$(Split-Path -Leaf $f):"
    Write-Output "  ORIG: open=$($origCounts[0]), close=$($origCounts[1]), diff=$($origCounts[0] - $origCounts[1])"
    Write-Output "  CURR: open=$($currCounts[0]), close=$($currCounts[1]), diff=$($currCounts[0] - $currCounts[1])"
}