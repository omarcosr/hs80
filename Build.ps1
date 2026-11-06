param([switch]$Run)
$ErrorActionPreference='Stop'
$taskPanel=Join-Path $PSScriptRoot 'dist\HS80-Controle.exe'
$taskWorker=Join-Path $PSScriptRoot 'dist\HS80-Leds.exe'
if(Get-Process -Name HS80-Controle -ErrorAction SilentlyContinue | Where-Object Path -eq $taskPanel){throw 'Feche o painel antes de recompilar.'}
$taskProcesses=@(Get-Process -Name HS80-Leds -ErrorAction SilentlyContinue | Where-Object Path -eq $taskWorker)
if($taskProcesses.Count){
 Start-Process -FilePath $taskWorker -ArgumentList '--stop' -WindowStyle Hidden -Wait
 foreach($taskProcess in $taskProcesses){Wait-Process -Id $taskProcess.Id -Timeout 12 -ErrorAction SilentlyContinue}
 if(Get-Process -Name HS80-Leds -ErrorAction SilentlyContinue | Where-Object Path -eq $taskWorker){throw 'O controle ainda está ativo. A compilação não foi iniciada.'}
}
Push-Location $PSScriptRoot
try{
 cmake --preset release
 if($LASTEXITCODE -ne 0){throw 'Falha ao configurar a compilação.'}
 cmake --build --preset release
 if($LASTEXITCODE -ne 0){throw 'Falha ao compilar ou preparar dist.'}
 ctest --preset release
 if($LASTEXITCODE -ne 0){throw 'Os testes não passaram.'}
 if($Run){Start-Process -FilePath $taskPanel}
}finally{Pop-Location}
