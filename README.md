# HS80 MAX — projeto enxuto em C e CUI

`dist/HS80-Controle.exe` abre o painel. `dist/HS80-Leds.exe --run` inicia somente o controle em segundo plano. Os arquivos de desenvolvimento ficam separados de `dist`.

## Compilar e executar

Na pasta deste projeto, com CMake e Visual Studio Build Tools 2026 (C/C++ e Windows SDK) instalados:

```powershell
.\Build.ps1 -Run
```

O script configura, compila em Release, prepara `dist`, roda os testes e abre o programa de `dist`. Para apenas compilar, omita `-Run`. Feche o painel antes de recompilar. O script encerra cooperativamente o controle desta mesma pasta antes de atualizar seus arquivos. As preferências existentes em `dist/hs80.ini` são preservadas.

Comandos equivalentes:

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
.\dist\HS80-Controle.exe
```

O destino CMake `dist` depende dos executáveis e dos testes e copia somente os dois programas, a configuração inicial, o guia e as licenças. Os objetos de compilação e os arquivos de teste ficam em `build`.

## Conteúdo do CUI

`vendor/CUI` é um recorte específico deste aplicativo, baseado na revisão `7bfb39ddaa5d0fd2dd9aacb0850d4767e2449d7f` de yappologistic/CUI. Não é o SDK completo. Foram mantidos os módulos de núcleo, tema, desenho, texto, layout, componentes básicos, controles, sobreposições, fontes incorporadas, descompressão, renderização por software e janela Win32. As fontes já estão incorporadas em `cui_font_data.c`; os arquivos TTF separados são dispensáveis.

Foram retirados outros backends, OpenGL, exemplos, demonstrações, gráficos, tabelas, editores avançados, carregadores de imagem e dependências não usadas. Os cabeçalhos públicos monolíticos mantêm declarações do toolkit; este recorte implementa o que o HS80 utiliza. O vínculo final descarta funções não usadas dos módulos restantes.

Além do ajuste anterior que evita redesenhos por mensagens de consulta do Windows, as partes OpenGL estão desabilitadas por `CUI_HS80_SOFTWARE_ONLY`. `CUI_HS80_ESSENTIALS` exclui métricas com gráficos, seletor pesquisável, paleta de comandos e diálogos/abertura de URLs, dispensando os módulos associados. O visual e a interação do painel são os mesmos. A licença MIT, as licenças das fontes e os avisos originais foram preservados. Os avisos de terceiros originais também descrevem partes do toolkit completo que não estão neste recorte.

## Controle do headset

Os comandos HID e a lógica de áudio permanecem iguais à versão já testada: receptor `1B1C:0A97`, headset filho `0A96`, LEDs pretos e acompanhamento do mudo físico. A bateria é consultada em décimos de porcentagem. O controlador pausa quando o iCUE está aberto e tenta reconectar após sua saída ou desconexão. Não grava áudio, usa a rede ou altera firmware.

`HS80-Leds.exe --stop` encerra normalmente. `--status` grava o diagnóstico gerado `estado-nativo.json`; esse arquivo não é necessário para executar. A inicialização automática é configurada pelo painel para o executável localizado nesta pasta `dist`.

O código original e a recuperação anterior continuam no projeto em C:; esta cópia não depende deles para compilar nem executar.
