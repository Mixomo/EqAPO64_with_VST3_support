param([int]$EditorPID,[string]$EditorPath)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$owned=Get-Process -Id $EditorPID
if($owned.Path -ne [IO.Path]::GetFullPath($EditorPath)){throw 'Unexpected editor process'}
$root=[System.Windows.Automation.AutomationElement]::FromHandle($owned.MainWindowHandle)
$items=$root.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)
$panels=@($items | Where-Object {$_.Current.AutomationId.EndsWith('.openPanelButton')})
if($panels.Count -ne 3){throw "Expected three fixture rows, got $($panels.Count)"}
foreach($item in $panels){if($item.Current.Name -ne 'Open panel'){throw 'Panel must start closed'};$item.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()}
