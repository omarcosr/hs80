# HS80 MAX — um executável e um processo

O produto é `dist/HS80.exe`. O mesmo processo mantém o controle do headset e abre uma única janela CUI sob demanda. Fechar a janela destrói a interface e devolve sua memória; o controle e o ícone perto do relógio continuam ativos.

## Compilar e executar

Com CMake, Visual Studio Build Tools 2026 e Windows SDK instalados:

```powershell
.\Build.ps1 -Run
```

O script compila em Release e roda os testes antes de parar o aplicativo em uso. Depois prepara `dist` e abre o executável único. Sem `-Run`, reabre o aplicativo caso já estivesse em uso. A configuração `dist/hs80.ini` é preservada. A inicialização habilitada para a versão anterior desta mesma pasta é migrada para `HS80.exe --run`.

Os comandos de compilação separados são:

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --build build --config Release --target dist -- /nr:false
```

Encerre o aplicativo antes do último comando de preparação de `dist`. O script faz isso automaticamente. Objetos, testes e mapas de vínculo ficam em `build`; apenas um executável de produto fica em `dist`.

## Execução

- `HS80.exe` ou `--show`: inicia o controle e abre a janela. Se já está em execução, abre a janela dentro do processo existente.
- `--run`: controle em segundo plano, sem criar a interface.
- `--hide`: fecha somente o painel, preservando o processo e o controle.
- `--stop`: encerra o aplicativo e devolve a iluminação ao hardware.
- `--status`: consulta o processo em execução e grava o diagnóstico gerado `estado-nativo.json`; esse arquivo não é uma dependência.
- `--capture`: captura de QA, somente quando a variável `CUI_SCREENSHOT` está definida; não inicia um segundo controlador.

## Eficiência e ciclo de vida

- CUI inicializado apenas ao abrir a janela, numa thread criada sob demanda dentro do mesmo processo.
- Alocações do CUI, fontes e renderizador em um heap privado do Windows, destruído após liberar os recursos nativos da janela. Nenhuma redução artificial do working set é usada.
- Renderização por software em uma thread; sem OpenGL, efeitos ou atualizações contínuas. Consultas não visuais do Windows não causam novos desenhos.
- A fonte monoespaçada não usada pelo painel não é compilada nem carregada. Inter, Geist e Lucide usados pela interface permanecem incorporados.
- O controlador consulta o estado compartilhado diretamente em memória dentro do processo. A interface continua respondendo enquanto a comunicação USB ocorre.
- Um evento de I/O por conexão HID é reutilizado, evitando criar e fechar objetos do Windows a cada pacote.
- Compilação estática em C, eliminação de código não usado e otimização entre módulos em Release.

Os testes verificam o protocolo, a validação do modelo, a escala da bateria e o ciclo de vida/semântica de alocação do heap da interface. Os testes de execução também conferem instância única, abertura/fechamento repetido, continuidade do controle e consumo real de memória/CPU.

## CUI incluído

Recorte de yappologistic/CUI na revisão `7bfb39ddaa5d0fd2dd9aacb0850d4767e2449d7f`. Não é o SDK completo. Mantém os módulos usados pelo HS80, Win32 e renderização por software; os cabeçalhos monolíticos ainda contêm declarações de componentes que não fazem parte deste recorte.

As adaptações são: espera de eventos não visuais, `CUI_HS80_SOFTWARE_ONLY`, exclusão de componentes não utilizados por `CUI_HS80_ESSENTIALS`, alocador privado `cui_hs80_memory`, exclusão de Geist Mono e uma thread de rasterização. As chamadas CRT de alocação próprias do CUI e os hooks de memória do stb são direcionados ao heap da janela. Callbacks de alocador fornecidos pelo contexto mantêm seus contratos originais.

Licença MIT, licenças das fontes e avisos de terceiros preservados. As notas originais também cobrem componentes do toolkit completo omitidos aqui. O desenho e os controles essenciais do painel permanecem iguais.

## Headset

Receptor `1B1C:0A97`, headset filho `0A96`. Os comandos de iluminação e a sincronização do mudo físico permanecem os já validados. O aplicativo não grava áudio, usa a rede ou altera firmware. Pausa o acesso quando o iCUE abre e tenta reconectar quando ele fecha ou o dispositivo retorna.

## Medições desta máquina

Amostras de 30 segundos, 20 processadores lógicos, sem alterar artificialmente o working set:

| Estado | RAM física | Memória privada | CPU total |
|---|---:|---:|---:|
| Inicialização em segundo plano, sem abrir a interface | 14,42 MiB | 2,10 MiB | 0,0156% |
| Painel aberto | 27,09 MiB | 7,59 MiB | 0,0234% |
| Painel fechado após oito ciclos | 23,32 MiB | 3,09 MiB | 0,0260% |

A memória total após usar a interface não retorna exatamente ao valor de partida, apesar da destruição do heap da janela. Os três estados estão registrados para não confundir a partida sem interface com a situação depois de usá-la. Os valores são amostras, não limites fixos.

A versão anterior, com dois processos, somava 9,79 MiB de memória privada com o painel aberto; a versão única mediu 7,59 MiB, cerca de 22,5% menos nesse indicador. A CPU permaneceu muito baixa; a amostra não estabelece uma redução garantida de CPU.

Foram verificados oito ciclos de abertura/fechamento mantendo o PID do controlador, cinco solicitações simultâneas de abertura sem outro processo residente, contadores de handles/GDI/USER estáveis entre os ciclos, encerramento com a janela aberta e reinício oculto. O resultado detalhado fica em `build/verificacao-executavel-unico.json`. O executável tem 512.512 bytes (500,5 KiB).
