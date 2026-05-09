$diff = git -C D:\workspace\ShootGame diff --no-color -- "UnrealEngine/Engine/Source/Runtime/Net/Iris/" 2>&1
$normalize = {
    param($l)
    # 移除开头 +/- 标记
    $r = $l.Substring(1)
    # 移除行内 // 注释（但不要破坏 URL 等）
    $r = $r -replace '\s*//[^"]*$',''
    # 折叠所有空白
    $r = $r -replace '\s+',' '
    return $r.Trim()
}
$codeRemovals = @()
$codeAdds = @()
foreach ($line in $diff) {
    if ($line -match "^---") { continue }
    if ($line -match "^\+\+\+") { continue }
    if ($line.StartsWith('-')) {
        $n = & $normalize $line
        if ($n -and -not $n.StartsWith('//') -and -not $n.StartsWith('*') -and -not $n.StartsWith('/*')) {
            $codeRemovals += $n
        }
    } elseif ($line.StartsWith('+')) {
        $n = & $normalize $line
        if ($n -and -not $n.StartsWith('//') -and -not $n.StartsWith('*') -and -not $n.StartsWith('/*')) {
            $codeAdds += $n
        }
    }
}
$addedSet = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($t in $codeAdds) { [void]$addedSet.Add($t) }
$truly = $codeRemovals | Where-Object { -not $addedSet.Contains($_) }
"Removed lines (after normalize): $($codeRemovals.Count)"
"Added lines (after normalize): $($codeAdds.Count)"
"Truly deleted (no normalized match in additions): $($truly.Count)"
""
"Sample (first 30 truly deleted):"
$truly | Select-Object -First 30 | ForEach-Object { "  $_" }