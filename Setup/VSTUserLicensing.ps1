param([ValidateSet('UI','Enable')][string]$Action='UI',[ValidatePattern('^S-1-5-21-[0-9-]+$')][string]$UserSid,[switch]$Elevated)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
try {
    if($Action -eq 'UI'){
        $answer=[Windows.Forms.MessageBox]::Show("The APO stays in LocalService. Out-of-process VST audio and panels run in your signed-in Windows account, where the vendor's licenses are activated.`r`n`r`nThis applies to all out-of-process VST2/VST3 modules, with no manufacturer list and no license copying. In-process modules keep their current mode. Your Windows session must remain signed in. The broker starts at Windows sign-in.`r`n`r`nEnable this mode?",'Use Windows user licenses',[Windows.Forms.MessageBoxButtons]::YesNo,[Windows.Forms.MessageBoxIcon]::Question)
        if($answer -ne [Windows.Forms.DialogResult]::Yes){exit 2}
        $Action='Enable'
    }
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
    if(-not $UserSid){$UserSid=$identity.User.Value}
    if($identity.User.Value -ne $UserSid){throw 'Use the same Windows account as the configuration editor. Different administrator credentials are not supported.'}
    $worker=Join-Path $PSScriptRoot 'EqApoOutProcHost.exe'
    if(-not (Test-Path -LiteralPath $worker -PathType Leaf)){throw 'EqApoOutProcHost.exe is missing. Install the complete application package.'}
    if(-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
        if($Elevated){throw 'Administrator permission was not granted.'}
        $child=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Action','Enable','-UserSid',$UserSid,'-Elevated') -PassThru
        $child.WaitForExit()
        if($child.ExitCode -eq 0){
            # Launch in the original ordinary user context, never in elevated setup.
            Start-Process -FilePath $worker -ArgumentList '--user-broker' -WindowStyle Hidden | Out-Null
        }
        exit $child.ExitCode
    }
    $machine=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,[Microsoft.Win32.RegistryView]::Registry64)
    $features=$machine.CreateSubKey('Software\EqualizerAPO\Features')
    $run=$machine.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Run',$true)
    try {
        $oldFlag=$features.GetValue('UserBroker',$null);$oldRun=$run.GetValue('EqualizerAPOUserBroker',$null)
        try {
            $features.SetValue('UserBroker',1,[Microsoft.Win32.RegistryValueKind]::DWord)
            $startup='"'+$worker+'" --user-broker'
            $run.SetValue('EqualizerAPOUserBroker',$startup,[Microsoft.Win32.RegistryValueKind]::String)
            if($features.GetValue('UserBroker') -ne 1 -or $run.GetValue('EqualizerAPOUserBroker') -cne $startup){throw 'Broker configuration verification failed.'}
        } catch {
            if($null -eq $oldFlag){$features.DeleteValue('UserBroker',$false)}else{$features.SetValue('UserBroker',$oldFlag,[Microsoft.Win32.RegistryValueKind]::DWord)}
            if($null -eq $oldRun){$run.DeleteValue('EqualizerAPOUserBroker',$false)}else{$run.SetValue('EqualizerAPOUserBroker',$oldRun,[Microsoft.Win32.RegistryValueKind]::String)}
            throw
        }
    } finally {$features.Dispose();$run.Dispose();$machine.Dispose()}
    exit 0
} catch {
    [Windows.Forms.MessageBox]::Show($_.Exception.Message,'Windows user licensing stopped',[Windows.Forms.MessageBoxButtons]::OK,[Windows.Forms.MessageBoxIcon]::Error) | Out-Null
    exit 1
}
