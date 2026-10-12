param(
    [ValidateSet('UI','Apply','Rollback')][string]$Action='UI',
    [switch]$AllowProfileReplacement,
    [ValidatePattern('^S-1-5-21-[0-9-]+$')][string]$UserSid,
    [switch]$Elevated
)
# Optional, vendor-specific registration of an existing user profile (may include activation and preferences).
# Never called by the audio engine; never changes service identity or ACLs.
$ErrorActionPreference='Stop'
if($Action -eq 'Apply' -and -not $AllowProfileReplacement){throw 'Registering the service profile requires explicit replacement authorization.'}
Add-Type -AssemblyName System.Security,System.Windows.Forms
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
if(-not $UserSid){$UserSid=$identity.User.Value}
if($identity.User.Value -ne $UserSid){throw 'Run this tool using the same Windows account that owns the Goodhertz profile. Different administrator credentials are not supported.'}
$administrator=([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(-not $administrator){
    if($Elevated){throw 'Administrator permission was not granted.'}
    $process=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-UserSid',$UserSid,'-Action',$Action,'-Elevated','-AllowProfileReplacement') -PassThru
    $process.WaitForExit();exit $process.ExitCode
}
$registry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::Users,[Microsoft.Win32.RegistryView]::Registry64)
$backupRegistry=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,[Microsoft.Win32.RegistryView]::Registry64)
$backupPath='Software\EqualizerAPO\GoodhertzActivation'
$journal=$UserSid
$transaction=New-Object Threading.Mutex($false,'Global\EqualizerAPO.GoodhertzActivation')
$locked=$false
try {$locked=$transaction.WaitOne(0)} catch [Threading.AbandonedMutexException] {$locked=$true}
if(-not $locked){$transaction.Dispose();$registry.Dispose();$backupRegistry.Dispose();throw 'Another registration is running. Close it before continuing.'}
function Has-Backup {
    $key=$backupRegistry.OpenSubKey($backupPath)
    try {return ($key -and $key.GetValueNames() -contains $journal)} finally {if($key){$key.Dispose()}}
}
function Read-Backup {
    $key=$backupRegistry.OpenSubKey($backupPath)
    try {
        if(-not $key -or $key.GetValueKind($journal) -ne [Microsoft.Win32.RegistryValueKind]::Binary){throw 'Invalid encrypted backup.'}
        return ,([byte[]]$key.GetValue($journal))
    } finally {if($key){$key.Dispose()}}
}
$versions=@('profile')
function Read-License([string]$Path){
    $key=$registry.OpenSubKey($Path)
    try {
        if(-not $key -or $key.GetValueNames() -notcontains 'JUCE.interface'){return [pscustomobject]@{Present=$false;Value=$null}}
        if($key.GetValueKind('JUCE.interface') -ne [Microsoft.Win32.RegistryValueKind]::String){throw 'Unexpected JUCE.interface value type; no changes made.'}
        return [pscustomobject]@{Present=$true;Value=[string]$key.GetValue('JUCE.interface')}
    } finally {if($key){$key.Dispose()}}
}
function Write-License($Record,[bool]$Restore){
    $key=$registry.CreateSubKey($Record.Path)
    try {
        if($Restore){
            if($Record.Present){$key.SetValue('JUCE.interface',[string]$Record.Value,[Microsoft.Win32.RegistryValueKind]::String)}
            else {$key.DeleteValue('JUCE.interface',$false)}
        } else {$key.SetValue('JUCE.interface',[string]$Record.Applied,[Microsoft.Win32.RegistryValueKind]::String)}
    } finally {$key.Dispose()}
    $actual=Read-License $Record.Path
    if($Restore){
        if($actual.Present -ne [bool]$Record.Present -or ($Record.Present -and $actual.Value -cne $Record.Value)){throw 'Restoration verification failed; keep the encrypted backup.'}
    } elseif($actual.Value -cne $Record.Applied){throw 'Activation verification failed.'}
}
function Save-Backup($Records){
    # Fixed native HKLM location: no elevated writes to user-controlled paths.
    $clear=[Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $Records -Compress))
    try {$encrypted=[Security.Cryptography.ProtectedData]::Protect($clear,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)}
    finally {[Array]::Clear($clear,0,$clear.Length)}
    $key=$backupRegistry.CreateSubKey($backupPath)
    try {
        if($key.GetValueNames() -contains $journal){throw 'Backup already exists.'}
        $key.SetValue($journal,$encrypted,[Microsoft.Win32.RegistryValueKind]::Binary)
        $key.Flush()
        if([Convert]::ToBase64String([byte[]]$key.GetValue($journal)) -cne [Convert]::ToBase64String($encrypted)){throw 'Backup verification failed.'}
    } finally {$key.Dispose()}
}
function Apply-Activation {
    $records=@()
    foreach($version in $versions){
        $source=Read-License "$UserSid\Software\Goodhertz"
        if(-not $source.Present -or [string]::IsNullOrWhiteSpace($source.Value)){continue}
        $path="S-1-5-19\Software\Goodhertz"
        $previous=Read-License $path
        if($previous.Value -ceq $source.Value){continue}
        if(-not $AllowProfileReplacement){throw 'Replacing the service profile has not been authorized.'}
        $records+=[pscustomobject]@{Path=$path;Present=$previous.Present;Value=$previous.Value;Applied=$source.Value}
    }
    if($records.Count -eq 0){return 'No changes: profile values match, or the source profile value is absent.'}
    if(Has-Backup){throw 'A previous encrypted backup exists. Undo that registration before starting a new one.'}
    Save-Backup $records
    try {
        foreach($record in $records){
            $now=Read-License $record.Path
            if($now.Present -ne $record.Present -or ($now.Present -and $now.Value -cne $record.Value)){throw 'Activation changed concurrently; operation stopped.'}
            Write-License $record $false
        }
    } catch {
        foreach($record in $records){
            $now=Read-License $record.Path
            if($now.Value -ceq $record.Applied){Write-License $record $true}
        }
        throw
    }
    return 'Registered the Goodhertz profile for LocalService. Original user profile is unchanged. Reload the affected VST. Undo restores the previous service value from its encrypted backup.'
}
function Undo-Activation {
    if(-not (Has-Backup)){return 'No registration backup exists for this Windows account.'}
    $clear=[Security.Cryptography.ProtectedData]::Unprotect((Read-Backup),$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
    try {$records=[Text.Encoding]::UTF8.GetString($clear) | ConvertFrom-Json} finally {[Array]::Clear($clear,0,$clear.Length)}
    if(@($records).Count -lt 1 -or @($records).Count -gt 1){throw 'Invalid backup.'}
    $seen=@()
    foreach($record in $records){
        if($record.Path -notin @('S-1-5-19\Software\Goodhertz') -or $record.Path -in $seen){throw 'Invalid backup destination.'}
        $seen+=$record.Path
        $now=Read-License $record.Path
        if($now.Value -cne $record.Applied -and ($now.Present -ne $record.Present -or ($now.Present -and $now.Value -cne $record.Value))){throw 'The service activation has changed since registration. Undo will not overwrite it.'}
    }
    foreach($record in $records){Write-License $record $true}
    $key=$backupRegistry.OpenSubKey($backupPath,$true)
    try {$key.DeleteValue($journal,$true)} finally {$key.Dispose()}
    return 'Restored the previous LocalService JUCE.interface values. Original user licenses are unchanged.'
}

try {
    if($Action -ne 'UI'){
        if($Action -eq 'Apply'){Write-Output (Apply-Activation)}else{Write-Output (Undo-Activation)}
        exit 0
    }
    $form=New-Object Windows.Forms.Form
    $form.Text='Goodhertz: LocalService registration'
    $form.ClientSize=New-Object Drawing.Size(720,320)
    $form.StartPosition='CenterScreen';$form.FormBorderStyle='FixedDialog';$form.MaximizeBox=$false
    $label=New-Object Windows.Forms.Label
    $label.SetBounds(20,20,680,220)
    $label.Text="Equalizer APO processes in-process VST audio as LocalService. Goodhertz may not see the activation in your Windows account and can return unchanged audio.`r`n`r`nRegister copies only your existing Goodhertz JUCE.interface profile value to LocalService on this computer. It may include activation and preferences. Use this for your own officially activated installation, according to the vendor's license terms.`r`n`r`nYour original Windows profile is unchanged. The previous service value is saved in an encrypted backup. Undo restores it. No Windows service, processing mode or VST preset is changed.`r`n`r`nReload the affected Goodhertz module after registering. If it still cannot process, use Undo and the Windows user licensing mode."
    $form.Controls.Add($label)
    foreach($item in @(@('Register','Apply',20),@('Undo','Rollback',250),@('Close','Close',480))){
        $button=New-Object Windows.Forms.Button;$button.Text=$item[0];$button.Tag=$item[1];$button.SetBounds($item[2],260,220,36)
        $button.Add_Click({
            if($this.Tag -eq 'Close'){$form.Close();return}
            try {
                if($this.Tag -eq 'Apply'){$script:AllowProfileReplacement=$true;$message=Apply-Activation}else{$message=Undo-Activation}
                [Windows.Forms.MessageBox]::Show($form,$message,'Goodhertz registration') | Out-Null
            } catch {[Windows.Forms.MessageBox]::Show($form,$_.Exception.Message,'Registration stopped',[Windows.Forms.MessageBoxButtons]::OK,[Windows.Forms.MessageBoxIcon]::Error) | Out-Null}
        });$form.Controls.Add($button)
    }
    $form.ShowDialog() | Out-Null
} catch {Write-Error $_.Exception.Message;exit 1}
