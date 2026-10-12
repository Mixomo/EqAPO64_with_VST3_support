param(
    [Parameter(Mandatory=$true)][int]$EditorPID,
    [Parameter(Mandatory=$true)][string]$EditorPath,
    [ValidateSet('inspect','power','panel','preamp')][string]$Action='inspect',
    [double]$Gain=0
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$ownedProcess=Get-Process -Id $EditorPID
if($ownedProcess.Path -ne [IO.Path]::GetFullPath($EditorPath)){throw 'Unexpected owned Editor executable'}
$rootElement=[System.Windows.Automation.AutomationElement]::FromHandle($ownedProcess.MainWindowHandle)
if(!$rootElement){throw 'Owned Editor window not ready'}
$elements=$rootElement.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)
$power=@();$panels=@();$gains=@()
foreach($element in $elements){
    $current=$element.Current
    if($current.Name -in @('Power on','Power off')){$power+=,$element}
    if($current.AutomationId.EndsWith('.openPanelButton')){$panels+=,$element}
    if($current.AutomationId.EndsWith('.doubleSpinBox')){$gains+=,$element}
}
if($power.Count -ne 2 -or $panels.Count -ne 1){throw "Expected one Preamp and one VST row, observed $($power.Count)/$($panels.Count)"}
switch($Action){
    'power' {$power[1].GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Toggle()}
    'panel' {
        if(!$panels[0].Current.IsEnabled){throw 'Panel action disabled during bypass'}
        $panels[0].GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    }
    'preamp' {
        if($gains.Count -ne 1){throw 'Expected exactly one Preamp value'}
        $pattern=$null
        if($gains[0].TryGetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern,[ref]$pattern)){$pattern.SetValue($Gain)}
        elseif($gains[0].TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern,[ref]$pattern)){$pattern.SetValue($Gain.ToString([Globalization.CultureInfo]::InvariantCulture))}
        else {throw 'Preamp does not expose an editable value pattern'}
    }
}
@{panel_name=$panels[0].Current.Name;panel_enabled=$panels[0].Current.IsEnabled;power_name=$power[1].Current.Name;editor_hwnd=[long]$ownedProcess.MainWindowHandle} | ConvertTo-Json -Compress
