# Snes9x NTSC Lie to PAL

Núcleo **Snes9x Libretro** com uma quarta opção de região, **NTSC (Lie to PAL)**,
para executar jogos com temporização NTSC enquanto o hardware emulado informa
PAL ao jogo. A **Beta 2** acrescenta Mode 7 Hi-Res, MSU-1 Enhanced Audio e
SuperFX Timing, e traz núcleos para **Android ARM64**, **Windows x64** e
**Linux ARM64 (ROCKNIX)**.

A idéia inicialmente era rodar **Donkey Kong Country 2 (Europe)** em NTSC, mas por conter muitos códigos anticopia em sua programação o jogo não se permitia avançar, e em alguns casos o emulador podia travar.
Agora com o modo NTSC lie to PAL, o jogo acredita que está rodando em PAL mas na verdade está com os Timmings do modo NTSC.

Foram feitos testes e funciona com os outros jogos PAL onde vc pode jogar em NTSC sem o jogo identificar a mudança de região, mostrar uma mensagem, simplesmente travar ou ficar com a tela preta.

## Recursos adicionais da Beta 2

- **Mode 7 Hi-Res:** 2x/4x horizontal e 2x/4x H+V, com filtros bilineares Stable/Smooth. A resolução horizontal é obtida por novas amostras da matriz de rotação. A vertical usa interpolação entre linhas; não é uma nova emulação sublinha. Mosaic mantém o efeito original. A saída 4x é reduzida para 512 pixels antes do filtro Blargg para respeitar o tamanho do buffer NTSC.
- **MSU-1 Enhanced Audio:** saída 44100 Hz para conteúdo MSU-1, ativada por padrão e aplicada ao recarregar o conteúdo. Jogos comuns e a opção desativada continuam em 32040 Hz. Requer os arquivos MSU-1 da versão do jogo.
- **SuperFX Timing:** seleção entre execução por ciclos e o orçamento legado por linha; funciona junto do controle de overclock. O padrão da base permanece `accurate`.
- Categorias System, Video, Audio, Input e Emulation, com fallback para frontends antigos. Os controles de volume individual não foram adicionados.

Os testes automatizados verificam resoluções e conteúdo de uma ROM Mode 7 original gerada, as três opções de filtragem, mudanças ao vivo e as frequências MSU-1. A regressão de região cobre 144 cargas, reset, save states e a leitura de STAT78. A qualidade e desempenho em jogos comerciais ainda precisam de teste no aparelho.

Os builds adicionais são núcleos Libretro para **Windows x64** e **Linux ARM64 (alvo ROCKNIX/RG DS)**. Não são emuladores standalone. O build Linux depende da compatibilidade glibc do sistema de destino; o carregamento no RG DS precisa de validação no aparelho.

## Downloads

As versões públicas e seus arquivos ficam em
[Releases do projeto](https://github.com/VrepliroidV/snes9x-ntsc-lie-pal/releases).
A versão atual é a
**[v1.0.0-beta.2 — Beta 2](https://github.com/VrepliroidV/snes9x-ntsc-lie-pal/releases/tag/v1.0.0-beta.2)**:

| Arquivo | Conteúdo |
| --- | --- |
| `snes9x-ntsc-lie-pal-v1.0.0-beta.2-android-windows-rocknix.zip` | Os três núcleos em pastas separadas, `LEIA-ME.txt`, licenças e relatórios de validação |
| `snes9x_libretro_android.so` | Núcleo Android ARM64 (RetroArch Android) |
| `snes9x_libretro.dll` | Núcleo Windows x64 (RetroArch Windows) |
| `snes9x_libretro.so` | Núcleo Linux ARM64 para ROCKNIX |
| `SHA256SUMS` | Hashes dos arquivos acima |

Use apenas o núcleo da sua plataforma. A
[Beta 1](https://github.com/VrepliroidV/snes9x-ntsc-lie-pal/releases/tag/v1.0.0-beta.1)
continua disponível, com o núcleo Android que traz apenas a opção NTSC (Lie to PAL).

## Por que uma opção adicional de região?

O SNES usa temporizações diferentes nas regiões NTSC e PAL: aproximadamente
**60,10 Hz** e **50,01 Hz**, respectivamente. Alguns jogos europeus verificam a
região do console antes de iniciar ou durante a execução. Forçar apenas NTSC
em uma ROM PAL pode acelerar a execução, mas também fazer o jogo identificar
uma região diferente daquela que espera.

A opção **NTSC (Lie to PAL)** separa duas informações: a região usada para os
relógios e a contagem de linhas da emulação, e a região que o jogo recebe ao
consultar o registrador de identificação da PPU. O objetivo é permitir
**temporização NTSC com identificação PAL** para jogos que se beneficiem dessa
combinação. Nenhuma ROM é modificada, e a opção não depende de patches IPS,
UPS ou BPS.

Forçar 60 Hz pode mudar a velocidade ou o comportamento de um jogo desenvolvido
para PAL. Esta modificação não converte automaticamente todos os aspectos de
um jogo PAL para NTSC.

## Como funciona

O bit 4 do registrador PPU **STAT78**, no endereço **`$213F`**, informa a região:
**0 para NTSC** e **1 para PAL**. A alteração está na leitura de `S9xGetPPU`,
em [ppu.cpp](ppu.cpp). O cálculo desse bit passa a usar:

```cpp
(Settings.PAL || Settings.NTSCLiePAL) ? 0x10 : 0
```

`0x10` é a máscara do bit 4. Ele fica ativo quando a emulação está realmente em
PAL ou quando a nova opção está ativa. Os outros bits e os efeitos da leitura de
STAT78 são preservados.

A implementação mantém as responsabilidades separadas:

- **Temporização:** `Settings.PAL` continua determinando a região efetiva,
  os relógios, a contagem de linhas e a região/FPS informados à interface Libretro.
  No novo modo, `ForceNTSC=true` e `ForcePAL=false`, resultando em temporização NTSC.
- **Região reportada ao jogo:** `Settings.NTSCLiePAL` altera somente o bit 4
  retornado pela leitura de STAT78. O frontend continua recebendo região NTSC.
- **Aplicação da opção:** a configuração é aplicada ao carregar o conteúdo,
  tanto no carregamento normal quanto em `retro_load_game_special`. A região
  reportada permanece fixa durante a sessão e muda no próximo carregamento.

O nome da opção exibido no menu é `NTSC (Lie to PAL)` e o valor salvo é
`ntsc_lie_pal`, dentro da configuração Libretro `snes9x_region`.

### Comparação das opções

| Opção | Valor salvo | Temporização | Bit 4 de `$213F` informado ao jogo |
| --- | --- | --- | --- |
| Auto | `auto` | Detecção original da região da ROM | 0 em NTSC; 1 em PAL |
| NTSC | `ntsc` | NTSC, aproximadamente 60,10 Hz | 0 — NTSC |
| PAL | `pal` | PAL, aproximadamente 50,01 Hz | 1 — PAL |
| NTSC (Lie to PAL) | `ntsc_lie_pal` | NTSC, aproximadamente 60,10 Hz | 1 — PAL |

**Auto, NTSC e PAL mantêm o comportamento original.** Os valores de frequência
acima vêm da temporização do núcleo; não são uma medição de desempenho no
aparelho usado no teste relatado.

## Plataformas

| Plataforma | Situação deste projeto |
| --- | --- |
| Android ARM64 / `arm64-v8a` | Núcleo da Beta 2 com validação ELF aprovada. NTSC (Lie to PAL) testado no aparelho na Beta 1; recursos novos ainda sem teste em jogos reais |
| Windows x64 | Núcleo Libretro (`.dll`) da Beta 2 com inspeção PE aprovada; sem teste em jogos reais |
| Linux ARM64 / ROCKNIX | Núcleo Libretro (`.so`) da Beta 2 com testes de recursos por QEMU; sem teste no RG DS |

O binário Android é compilado para **API 21 ou superior** (Android 5.0+), com
libc++ estático e segmentos ELF alinhados a 16 KB. Esses são requisitos e
propriedades do build, não uma validação em todas as versões do Android ou em
todos os aparelhos. O RetroArch precisa executar como **aarch64/64 bits**.

O núcleo Windows é compilado com MinGW em C++17, com runtime estático; depende
apenas de `KERNEL32.dll` e `msvcrt.dll`. O núcleo Linux ARM64 requer
**glibc 2.29 ou posterior** e `libstdc++.so.6` com `GLIBCXX_3.4.29`. O QEMU não
reproduz o ambiente completo do ROCKNIX nem mede desempenho no portátil.

Os três são núcleos Libretro para usar no RetroArch, não emuladores standalone.
O build Linux x86_64 dos testes automatizados serve apenas para verificar a
emulação e não é distribuído.

## Instalação no RetroArch Android

1. Use o [RetroArch oficial para Android aarch64](https://docs.libretro.com/guides/install-android/).
   Um aparelho ARM64 pode executar aplicativos de 32 bits; confirme que sua
   instalação do RetroArch é de 64 bits.
2. Faça backup do núcleo Snes9x existente em **Configurações > Núcleo > Gerenciar
   núcleos > Snes9x > Fazer cópia de segurança**. O arquivo fornecido usa o nome
   `snes9x_libretro_android.so` e **substitui o Snes9x instalado**.
3. Baixe o `.so` ou extraia o ZIP da versão (pasta `Android_ARM64`) e copie o
   arquivo para uma pasta acessível pelo RetroArch. Desbloqueie o núcleo
   existente antes da instalação, caso esteja bloqueado.
4. Abra **Menu principal > Carregar núcleo > Instalar ou restaurar núcleo**,
   selecione `snes9x_libretro_android.so` e aguarde a conclusão. O aplicativo
   copia o arquivo para seu diretório de núcleos; não é necessário root.
5. Carregue Snes9x e sua ROM original. Se o arquivo não aparecer no seletor,
   confira **Configurações > Diretório > Downloads**: a pasta configurada pode
   ser diferente da pasta Download do Android.
6. Após instalar, considere bloquear o núcleo em **Gerenciar núcleos** para que
   o atualizador não o substitua pela versão original.

Os nomes dos menus podem variar conforme a versão e o idioma do RetroArch.

## Instalação no Windows e no ROCKNIX

Feche o RetroArch antes de substituir um núcleo e faça backup do Snes9x
existente, porque o arquivo deste projeto usa o mesmo nome.

- **Windows x64:** copie `snes9x_libretro.dll` (pasta `Windows_x64` do ZIP) para
  o diretório de núcleos do RetroArch, normalmente a pasta `cores`, e carregue o
  núcleo Snes9x.
- **ROCKNIX (Linux ARM64):** copie `snes9x_libretro.so` (pasta `ROCKNIX_ARM64`
  do ZIP) para o diretório de núcleos configurado no RetroArch. O caminho e a
  permissão de escrita dependem da instalação do sistema.

Depois de copiar, carregue o núcleo e o jogo novamente.

## Opções do núcleo

| Categoria | Opção | Valores |
| --- | --- | --- |
| System | Console Region | Auto, NTSC, PAL, **NTSC (Lie to PAL)** |
| Video | Mode 7 Hi-Res | disabled, 2x, 4x, 2x (H+V), 4x (H+V) |
| Video | Mode 7 Hi-Res Filtering | disabled, Stable, Smooth |
| Video | Auto Crop Black Borders | disabled (padrão), Fit (keep proportions), Stretch (fill frame) |
| Audio | MSU-1 Enhanced Audio | enabled (padrão), disabled; recarregue o conteúdo após mudar |
| Emulation | SuperFX Timing | Cycle Accurate (padrão), Legacy |

A localização e os nomes das categorias podem depender do frontend. O MSU-1
Enhanced Audio não cria trilhas: o jogo precisa do patch e dos arquivos MSU-1
compatíveis.

### Auto Crop Black Borders

Detecta bordas pretas ao redor da imagem do jogo e mostra só a área do jogo. Cada
tela é avaliada separadamente, então telas sem borda no mesmo jogo continuam
inteiras.

- **Fit (keep proportions):** amplia a área do jogo sem deformar os pixels.
- **Stretch (fill frame):** estica a área do jogo até preencher o quadro original.
- Linhas desenhadas com a tela desligada (forced blank, brilho zero ou sem camadas)
  são reconhecidas pelo próprio núcleo e cortadas após 6 quadros estáveis.
- Bordas que são apenas pixels pretos, como tiles pretos ou janelas, precisam
  ficar iguais por 60 quadros (cerca de 1 segundo). Elas são ignoradas em telas
  com muito preto, como texto sobre fundo preto.
- Se algum gráfico aparecer na área cortada, a imagem inteira volta no mesmo
  quadro. Telas totalmente pretas, como fades, mantêm o corte atual.
- Funciona com hi-res, Mode 7 Hi-Res, entrelaçado, filtro Blargg e com Crop
  Overscan desativado. A mira das pistolas de luz acompanha o corte.

No RetroArch, deixe a proporção de tela (**Aspect Ratio**, nas opções de escala
de vídeo) em **Core provided**. Com outra proporção fixa, o próprio RetroArch
estica a imagem cortada.

### Ativar NTSC (Lie to PAL)

1. Com o jogo carregado, abra **Menu rápido > Opções do núcleo**.
2. Em **Console Region (Reload Core)**, selecione **NTSC (Lie to PAL)**.
3. Salve as opções para o jogo ou para o núcleo, conforme sua preferência.
4. **Feche o conteúdo e recarregue o núcleo e o jogo.** A região é aplicada no
   carregamento; apenas mudar a opção durante a execução não aplica a nova região.

O valor correspondente em um arquivo de opções é:

```ini
snes9x_region = "ntsc_lie_pal"
```

Para a primeira validação, inicie o jogo sem carregar um estado salvo de outro
modo de região. Estados devem ser criados e restaurados no mesmo modo: estados
PAL podem conter temporizações incompatíveis com uma sessão NTSC.

## Resultados de validação

### Beta 2

Os quatro jobs de CI do [PR #3](https://github.com/VrepliroidV/snes9x-ntsc-lie-pal/pull/3),
`region-regression`, `android-arm64`, `windows-x64` e `rocknix-arm64`,
concluíram com sucesso. Os relatórios acompanham os núcleos no ZIP da versão:

- **Região:** 144 carregamentos de ROM de diagnóstico, cobrindo STAT78, contagem
  de linhas NTSC/PAL, FPS, reset, save state e recarga.
- **Mode 7 Hi-Res:** 15 combinações de escala e filtragem, com dimensões e
  conteúdo conferidos, diferença real contra simples duplicação de pixels e
  troca ao vivo.
- **MSU-1:** detecção pelo arquivo `.msu` e saída em 44100/32040 Hz, com retorno
  correto a um jogo comum.
- **SuperFX:** ROM gerada executada nos dois modos, com ritmos distintos e troca
  ao vivo.
- **Builds:** validação ELF, importações e exportações no Android; testes de
  recursos por QEMU no Linux ARM64; inspeção PE, exportações e dependências no
  Windows.

Os recursos novos ainda precisam de teste em jogos reais no Android, no Windows
e no RG DS.

### Beta 1 — testes automatizados

A [execução aprovada do Actions](https://github.com/VrepliroidV/snes9x-ntsc-lie-pal/actions/runs/37941390187)
usou o commit `1cc6195dc4a2449679ec7d47994748301a5d5ee6`. Os dois jobs,
`region-regression` e `android-arm64`, concluíram com sucesso.

Os testes de integração executam uma ROM sintética original, gerada em memória,
sem ROM comercial. Passaram **72 cenários e 144 carregamentos**, cobrindo:

- Os quatro modos de região com cabeçalhos NTSC e PAL.
- O bit 4 recebido pela CPU emulada ao ler `$213F`.
- Temporização real observada pelos contadores verticais: máximo de 261 em NTSC
  e 311 em PAL, inclusive NTSC quando o novo modo reporta PAL.
- FPS e `retro_get_region()`, reset, restauração de estado e da WRAM.
- Mudanças de opção aplicadas após recarregar o conteúdo.
- Frontends de opções 2/1/legado e definições em inglês/turco. Na Beta 1,
  frontends que anunciam versão 2 recebiam a interface de opções v1 do upstream;
  a partir da Beta 2 eles recebem as categorias, com fallback v1/legado.

O build Android foi vinculado e passou na inspeção ELF: AArch64, API 21,
segmentos alinhados a 16 KB, 25 funções obrigatórias da API Libretro e
importações verificadas contra os stubs Android API 21. As dependências de
runtime são apenas `libc.so`, `libdl.so` e `libm.so`.

### Beta 1 — teste real no RetroArch Android

O responsável pelo projeto relatou o seguinte teste com o núcleo do Actions:

| Item | Resultado informado |
| --- | --- |
| Plataforma | Android ARM64, no RetroArch Android |
| ROM | Original `Donkey Kong Country 2 (Europe) (Rev 1)` |
| Opção no menu | `NTSC (Lie to PAL)` apareceu e funcionou |
| Progresso | Primeira fase concluída; segunda fase desbloqueada normalmente |

Esse resultado valida o funcionamento no trecho jogado. Modelo do aparelho,
versões do Android/RetroArch e medição de FPS não foram informados. A campanha
completa e outros jogos ainda precisam de validação.

Os detalhes de origem, inspeção e limites estão em
[docs/VALIDATION.md](docs/VALIDATION.md).

## Limitações conhecidas

- A opção altera a identificação PAL de STAT78; jogos que verificam outros
  aspectos do hardware ou dependem de comportamento específico de 50 Hz podem
  continuar incompatíveis ou apresentar diferenças de velocidade/sincronização.
- Não há garantia de compatibilidade universal. O teste comercial relatado
  cobre DKC2 Europe Rev 1 até o desbloqueio da segunda fase.
- Trocar a região exige recarregar o conteúdo. Estados de outro modo de região
  não devem ser usados como prova de compatibilidade.
- Desempenho sustentado, sincronização de áudio, outros aparelhos e versões do
  RetroArch, campanhas completas e outros jogos ainda precisam de validação.
- O alinhamento de 16 KB foi verificado no ELF; não foi informado um teste em
  aparelho com páginas de 16 KB.
- Mode 7 Hi-Res, MSU-1 Enhanced Audio e SuperFX Timing foram verificados com
  ROMs geradas nos testes automatizados, mas ainda não em jogos comerciais nos
  aparelhos. Os núcleos Windows e ROCKNIX também aguardam teste real.
- A interpolação vertical do Mode 7 (H+V) é um pós-processamento entre linhas,
  não uma nova emulação por sublinha. A combinação 4x + filtro Blargg é reduzida
  para 512 pixels antes do filtro NTSC.
- Volumes individuais de canais não foram adicionados.

Para relatar problemas, abra uma
[issue](https://github.com/VrepliroidV/snes9x-ntsc-lie-pal/issues) com jogo/revisão,
modo de região, modelo do aparelho, versões do Android e RetroArch, versão ou
SHA256 do núcleo e os logs. **Não anexe ROMs comerciais.** O
[guia oficial de logs do RetroArch](https://docs.libretro.com/guides/generating-retroarch-logs/)
explica como coletá-los.

## Código-fonte e compilação

A base é o código oficial de [Snes9x](https://github.com/snes9xgit/snes9x),
revisão [1bcc369e89f08243e0a462882fb1f3e42e51de3a](https://github.com/snes9xgit/snes9x/commit/1bcc369e89f08243e0a462882fb1f3e42e51de3a),
Snes9x 1.63. O histórico oficial foi preservado no commit de importação
`6c43020e`; a modificação funcional está em `827fb71c`. O commit `1cc6195d`
acrescentou avisos de licença e preservação de logs no CI.

Os recursos da Beta 2 entraram pelo
[PR #3](https://github.com/VrepliroidV/snes9x-ntsc-lie-pal/pull/3). A
implementação está no commit `31e37578`; as revisões seguintes, `631704b2` e
`8b06d77b`, ajustam apenas a compilação Windows.

O [workflow Android ARM64](.github/workflows/android-arm64.yml) usa
**Android NDK r28c / 28.2.13676358**, `ndk-build`, `arm64-v8a`, `android-21`,
`c++_static`, suporte a páginas de 16 KB e link com `--no-undefined`.
Não é necessário inicializar os submódulos das interfaces desktop para este núcleo.

O [workflow Windows e ROCKNIX](.github/workflows/desktop-rocknix.yml) compila o
núcleo Windows x64 com MinGW (`platform=win`, C++17, link estático) e o núcleo
Linux ARM64 com `aarch64-linux-gnu-g++` (`platform=unix`). Em seguida, executa
`tests/features_test.cpp` no núcleo ARM64 por QEMU.

Para compilar o núcleo Android em um host Linux x86_64 com Git, Make, Python 3 e
o NDK instalado:

```sh
git clone https://github.com/VrepliroidV/snes9x-ntsc-lie-pal.git
cd snes9x-ntsc-lie-pal
export ANDROID_NDK_HOME=/caminho/android-ndk-r28c
bash scripts/build-android-arm64.sh
```

A saída fica em `build/android-arm64/`, incluindo o `.so`, checksums, metadados,
log, relatórios e licenças. O artefato de CI também inclui `android-core-smoke`,
um verificador opcional de carregamento. Na Beta 2, ele acompanha o núcleo na
pasta `Android_ARM64` do ZIP; não é necessário para instalar.

Para reproduzir os testes de região no host:

```sh
bash ci/test-region.sh
```

Uma nova compilação pode produzir outro hash e identificar outro commit. Os
núcleos publicados não são recompilados no empacotamento. A origem e os hashes
de cada versão ficam registrados nos arquivos do lançamento e nas notas da
[Beta 1](docs/releases/v1.0.0-beta.1.md) e da
[Beta 2](docs/releases/v1.0.0-beta.2.md).

## Licenças e créditos

Este projeto deriva de Snes9x e mantém os avisos originais. A
[licença Snes9x](LICENSE) permite uso, cópia, modificação e distribuição para
**fins não comerciais**, conforme seus termos completos. Os componentes
incluídos mantêm suas próprias licenças; o projeto não é relicenciado como MIT
ou GPL em sua totalidade.

Os pacotes do núcleo preservam `LICENSE`, a licença do filtro `snes_ntsc` e os
avisos do NDK/runtime C++. O código-fonte e os avisos dos componentes estão no
repositório; consulte também [filter/snes_ntsc-license.txt](filter/snes_ntsc-license.txt).

Créditos:

- [Snes9x](https://github.com/snes9xgit/snes9x) e seus autores/contribuidores pelo
  emulador original.
- [Libretro](https://www.libretro.com/) e
  [RetroArch](https://github.com/libretro/RetroArch) pela interface e pelo frontend.
- Hans-Kristian Arntzen e Daniel De Matteis, creditados no port Libretro original.
- Shay Green pelos componentes de áudio/filtro, e os demais autores listados
  nos avisos originais.
- [VrepliroidV](https://github.com/VrepliroidV) pela manutenção deste projeto e
  pelo teste relatado no RetroArch Android.

O projeto não distribui ROMs comerciais e não é um lançamento oficial do Snes9x
ou do Libretro.
