$ErrorActionPreference = 'Stop'

function Get-Skeleton {
    param([string]$content)
    # 移除 /* ... */ 块注释（非贪婪，含跨行）
    $content = [System.Text.RegularExpressions.Regex]::Replace($content, '/\*[\s\S]*?\*/', '')
    # 移除 // ... 行尾注释
    $content = [System.Text.RegularExpressions.Regex]::Replace($content, '//[^\r\n]*', '')
    # 仅保留可见 ASCII（剔除 CJK 字符 + 其它）
    # 然后只保留代码骨架（标识符、运算符、括号）
    # 移除全部空白
    $content = [System.Text.RegularExpressions.Regex]::Replace($content, '\s+', '')
    return $content
}

$samples = @(
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Public/Iris/ReplicationSystem/ReplicationSystem.h',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/ReplicationSystem/ReplicationSystem.cpp',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Public/Iris/Serialization/NetSerializer.h',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/Serialization/StringNetSerializers.cpp',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/ReplicationSystem/ReplicationWriter.cpp',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/ReplicationSystem/NetRefHandleManager.cpp',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/Core/IrisDebugging.cpp',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/IrisCoreModule.cpp',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/DataStream/DataStreamManager.cpp',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/ReplicationSystem/Filtering/ReplicationFiltering.cpp',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/ReplicationState/ReplicationStateDescriptorBuilder.cpp',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Public/Iris/ReplicationState/ReplicationStateDescriptor.h',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Public/Iris/Core/NetObjectReference.h',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Public/Iris/Serialization/NetBitStreamWriter.h',
    'UnrealEngine/Engine/Source/Runtime/Net/Iris/Private/Iris/Serialization/PackedVectorNetSerializers.cpp'
)

$totalOk = 0
$totalDiff = 0
$diffFiles = @()

foreach ($file in $samples) {
    $original = git -C D:\workspace\ShootGame show "HEAD:$file" 2>$null | Out-String
    $currentPath = "D:\workspace\ShootGame\$($file -replace '/','\')"
    $current = [System.IO.File]::ReadAllText($currentPath, [System.Text.Encoding]::UTF8)
    
    if (-not $original -or -not $current) {
        "[MISSING] $file"
        continue
    }
    
    $origSk = Get-Skeleton $original
    $currSk = Get-Skeleton $current
    
    if ($origSk -eq $currSk) {
        "[OK] $(Split-Path -Leaf $file) (skeleton=$($origSk.Length) chars)"
        $totalOk++
    } else {
        "[DIFF] $(Split-Path -Leaf $file) (orig=$($origSk.Length) curr=$($currSk.Length) delta=$($currSk.Length - $origSk.Length))"
        $totalDiff++
        $diffFiles += $file
        # 找第一个不同处
        $minLen = [Math]::Min($origSk.Length, $currSk.Length)
        $i = 0
        while ($i -lt $minLen -and $origSk[$i] -eq $currSk[$i]) { $i++ }
        $startCtx = [Math]::Max(0, $i - 30)
        $endOrig = [Math]::Min($origSk.Length - 1, $i + 60)
        $endCurr = [Math]::Min($currSk.Length - 1, $i + 60)
        if ($i -lt $minLen) {
            "  Diverge at char ${i}:"
            "  ORIG: ...$($origSk.Substring($startCtx, $endOrig - $startCtx + 1))..."
            "  CURR: ...$($currSk.Substring($startCtx, $endCurr - $startCtx + 1))..."
        } else {
            "  One side is a prefix of the other; difference at end"
        }
    }
}

""
"=== Summary ==="
"OK: $totalOk / $($samples.Count)"
"DIFF: $totalDiff / $($samples.Count)"