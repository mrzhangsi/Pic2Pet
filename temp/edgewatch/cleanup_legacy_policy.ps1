# 清理旧策略版留下的 Edge 策略键。
#
# 背景：Pic2Pet 的第一版网站白名单会往 Edge 官方策略写值，后来已整体废弃
# （改为只读检测 + 弹窗，新版一个注册表值都不写）。本机还留着当时测试的值。
#
# 只删「值数据恰好等于 preset.example.com」的那几个值 —— 那是 Pic2Pet 测试用的
# 假域名，绝不可能是你或组织配置的。碰到任何别的值一律保留。
#
# 用法：双击运行，或在 PowerShell 里执行
#   powershell -ExecutionPolicy Bypass -File temp\edgewatch\cleanup_legacy_policy.ps1

$ErrorActionPreference = 'Continue'

# ---- 自动提权（写 HKLM 需要管理员）----
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $id.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "[i] 不是管理员，正在重新以管理员身份启动..."
    $self = $MyInvocation.MyCommand.Path
    Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$self`""
    )
    Write-Host "[i] 已在另一个窗口启动，完成后关掉本窗口即可。"
    return
}

$TargetValue = 'preset.example.com'   # Pic2Pet 测试用的假域名

function Remove-OurValue {
    param([string]$KeyPath)

    if (-not (Test-Path $KeyPath)) {
        Write-Host "[skip]   $KeyPath 不存在"
        return
    }

    $key = Get-Item $KeyPath
    $names = $key.GetValueNames()

    # 找出值数据 == preset.example.com 的那些值名（以及它们的名字）
    $ours = @()
    foreach ($n in $names) {
        if ([string]$key.GetValue($n) -ieq $TargetValue) { $ours += $n }
    }

    if ($ours.Count -eq 0) {
        Write-Host "[keep]   $KeyPath 没有我们写的值（现有: $($names -join ', ')），不动"
        return
    }

    foreach ($n in $ours) {
        Remove-ItemProperty -Path $KeyPath -Name $n -Force
        Write-Host ("[remove] {0}\{1}" -f $KeyPath, $n)
    }
}

function Remove-KeyIfEmpty {
    param([string]$KeyPath)

    if (-not (Test-Path $KeyPath)) { return }
    $key = Get-Item $KeyPath
    if ($key.ValueCount -eq 0 -and $key.SubKeyCount -eq 0) {
        Remove-Item -Path $KeyPath -Force -Recurse
        Write-Host "[remove] 空键 $KeyPath"
    }
}

Write-Host "=== 清理 Pic2Pet 旧策略版残留（只动 $TargetValue）==="
Write-Host ""

# 1. Edge 策略值
Remove-OurValue 'HKLM:\SOFTWARE\Policies\Microsoft\Edge\URLBlocklist'
Remove-OurValue 'HKLM:\SOFTWARE\Policies\Microsoft\Edge\URLAllowlist'
Remove-KeyIfEmpty 'HKLM:\SOFTWARE\Policies\Microsoft\Edge\URLBlocklist'
Remove-KeyIfEmpty 'HKLM:\SOFTWARE\Policies\Microsoft\Edge\URLAllowlist'
Remove-KeyIfEmpty 'HKLM:\SOFTWARE\Policies\Microsoft\Edge'

# 2. 我们自己的备份键（整棵子树都是我们写的）
$backup = 'HKLM:\SOFTWARE\Pic2Pet'
if (Test-Path $backup) {
    $allText = (reg query $backup /s 2>$null) -join "`n"
    $foreign = @()
    if ($allText -match 'preset\.example\.com') {
        # 只有当整棵子树里出现我们的标记值时才敢整棵删
        # （标记值是我们写下的前提：有它才证明这块是我们创建的）
        Remove-Item -Path $backup -Force -Recurse
        Write-Host "[remove] $backup (含 WebFilterBackup 备份树)"
    } else {
        Write-Host "[keep]   $backup 未找到标记值，保守起见不动"
        $foreign += $backup
    }
} else {
    Write-Host "[skip]   $backup 不存在"
}

Write-Host ""
Write-Host "=== 结果 ==="
foreach ($p in @('HKLM:\SOFTWARE\Policies\Microsoft\Edge',
                 'HKLM:\SOFTWARE\Policies\Microsoft\Edge\URLBlocklist',
                 'HKLM:\SOFTWARE\Policies\Microsoft\Edge\URLAllowlist',
                 'HKLM:\SOFTWARE\Pic2Pet')) {
    if (Test-Path $p) {
        Write-Host "[REMAIN] $p"
        (Get-Item $p).GetValueNames() | ForEach-Object { Write-Host "           值 $_ = $((Get-ItemProperty $p).$_)" }
    } else {
        Write-Host "[GONE]   $p"
    }
}

Write-Host ""
Write-Host "如果 Edge 正开着，策略要重启 Edge（或等它重读）才会解除。"
Write-Host "按回车关闭。"
Read-Host | Out-Null
