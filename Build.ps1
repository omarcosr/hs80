param([switch]$Run)
$ErrorActionPreference='Stop'
$taskDist=Join-Path $PSScriptRoot 'dist'
$taskExe=Join-Path $taskDist 'HS80.exe'
$taskLegacyWorker=Join-Path $taskDist 'HS80-Leds.exe'
$taskLegacyPanel=Join-Path $taskDist 'HS80-Controle.exe'
Push-Location $PSScriptRoot
try{
 cmake --preset release
 if($LASTEXITCODE -ne 0){throw 'Falha ao configurar a compilação.'}
 cmake --build --preset release
 if($LASTEXITCODE -ne 0){throw 'Falha ao compilar.'}
 ctest --preset release
 if($LASTEXITCODE -ne 0){throw 'Os testes não passaram; a versão em uso foi preservada.'}
 $taskApps=@(Get-Process -Name HS80,HS80-Leds,HS80-Controle -ErrorAction SilentlyContinue | Select-Object Id,Path,MainWindowHandle | Where-Object {$_.Path -in @($taskExe,$taskLegacyWorker,$taskLegacyPanel)})
 $taskVisible=@($taskApps | Where-Object MainWindowHandle -ne 0).Count -gt 0
 foreach($taskApp in $taskApps | Where-Object Path -eq $taskLegacyPanel){
  $taskLive=Get-Process -Id $taskApp.Id -ErrorAction SilentlyContinue
  if($taskLive -and $taskLive.Path -eq $taskApp.Path){$taskLive.CloseMainWindow()|Out-Null}
  Wait-Process -Id $taskApp.Id -Timeout 12 -ErrorAction SilentlyContinue
 }
 foreach($taskApp in $taskApps | Where-Object Path -ne $taskLegacyPanel){
  Start-Process -FilePath $taskApp.Path -ArgumentList '--stop' -WindowStyle Hidden -Wait
  Wait-Process -Id $taskApp.Id -Timeout 12 -ErrorAction SilentlyContinue
 }
 if(Get-Process -Name HS80,HS80-Leds,HS80-Controle -ErrorAction SilentlyContinue | Where-Object {$_.Path -in @($taskExe,$taskLegacyWorker,$taskLegacyPanel)}){throw 'O aplicativo ainda está ativo; dist não foi atualizada.'}
 cmake --build build --config Release --target dist --parallel 4 -- /nr:false
 if($LASTEXITCODE -ne 0){throw 'Falha ao preparar dist.'}
 $taskMigration=Start-Process -FilePath $taskExe -ArgumentList '--migrate-startup' -WindowStyle Hidden -Wait -PassThru
 if($Run -or $taskVisible){Start-Process -FilePath $taskExe}
 elseif($taskApps.Count){Start-Process -FilePath $taskExe -ArgumentList '--run' -WindowStyle Hidden}
 if($taskMigration.ExitCode -ne 0){throw 'Falha ao migrar a inicialização do HS80 para o Agendador de Tarefas.'}
}finally{Pop-Location}
