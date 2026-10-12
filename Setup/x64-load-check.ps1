param(
	[string]$DllPath,
	[string]$LogPath
)

$ErrorActionPreference = "Stop"

Add-Type -Namespace EqApoInstaller -Name NativeMethods -MemberDefinition @"
	[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
	public static extern System.IntPtr LoadLibrary(string lpFileName);

	[System.Runtime.InteropServices.DllImport("kernel32.dll")]
	public static extern bool FreeLibrary(System.IntPtr hModule);
"@

function Write-Log($line) {
	Add-Content -LiteralPath $LogPath -Value $line
}

Write-Log ""
Write-Log "64-bit LoadLibrary diagnostic"
Write-Log "Path: $DllPath"

$handle = [EqApoInstaller.NativeMethods]::LoadLibrary($DllPath)
if ($handle -eq [IntPtr]::Zero) {
	$errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
	$message = (New-Object ComponentModel.Win32Exception($errorCode)).Message
	Write-Log "LoadLibrary failed. GetLastError=$errorCode. Message: $message"
	exit 1
}

[void][EqApoInstaller.NativeMethods]::FreeLibrary($handle)
Write-Log "LoadLibrary OK."
exit 0
