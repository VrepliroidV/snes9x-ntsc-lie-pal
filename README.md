# Snes9x NTSC Lie to PAL

Núcleo **Snes9x Libretro** com uma quarta opção de região, **NTSC (Lie to PAL)**,
para executar jogos com temporização NTSC enquanto o hardware emulado informa
PAL ao jogo. O primeiro alvo do projeto é **Android ARM64 no RetroArch**.

A implementação foi compilada no GitHub Actions e testada no RetroArch Android
com a ROM original **Donkey Kong Country 2 (Europe) (Rev 1)**: a primeira fase
foi concluída e a segunda foi desbloqueada normalmente. A validação de outros
jogos permanece em aberto.

## Downloads

As versões públicas e seus arquivos ficam em
[Releases do projeto](https://github.com/VrepliroidV/snes9x-ntsc-lie-pal/releases).
O primeiro lançamento, **v1.0.0-beta.1 — Android ARM64 Beta 1**, está em preparação.

Os arquivos previstos para essa versão são:

- `snes9x_libretro_android.so`: núcleo Android ARM64 para instalação no RetroArch.
- `snes9x-ntsc-lie-pal-v1.0.0-beta.1-android-arm64.zip`: o mesmo núcleo, com
  instruções básicas, informações de origem e avisos de licença.
- `SHA256SUMS`: hashes dos arquivos para conferir a integridade do download.

Até a publicação, o artefato aprovado está disponível na
[execução 37941390187 do GitHub Actions](https://github.com/VrepliroidV/snes9x-ntsc-lie-pal/actions/runs/37941390187).
É necessário entrar no GitHub para baixar artefatos de Actions. Extraia o ZIP
antes de selecionar o `.so` no RetroArch.

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
| Android ARM64 / `arm64-v8a` | Compilação aprovada no Actions e teste real no RetroArch Android |
| Windows x64 | Planejado; sem artefato ou teste desta modificação nessa plataforma |
| Linux ARM64 / Rocknix | Planejado; sem artefato ou teste desta modificação nessa plataforma |

O binário Android é compilado para **API 21 ou superior** (Android 5.0+), com
libc++ estático e segmentos ELF alinhados a 16 KB. Esses são requisitos e
propriedades do build, não uma validação em todas as versões do Android ou em
todos os aparelhos. O RetroArch precisa executar como **aarch64/64 bits**.

O build Linux x86_64 dos testes automatizados serve para verificar a emulação;
ele não é uma distribuição pública Windows ou Rocknix deste projeto.

## Instalação no RetroArch Android

1. Use o [RetroArch oficial para Android aarch64](https://docs.libretro.com/guides/install-android/).
   Um aparelho ARM64 pode executar aplicativos de 32 bits; confirme que sua
   instalação do RetroArch é de 64 bits.
2. Faça backup do núcleo Snes9x existente em **Configurações > Núcleo > Gerenciar
   núcleos > Snes9x > Fazer cópia de segurança**. O arquivo fornecido usa o nome
   `snes9x_libretro_android.so` e **substitui o Snes9x instalado**.
3. Baixe o `.so` ou extraia o ZIP da versão e copie o arquivo para uma pasta
   acessível pelo RetroArch. Desbloqueie o núcleo existente antes da instalação,
   caso esteja bloqueado.
4. Abra **Menu principal > Carregar núcleo > Instalar ou restaurar núcleo**,
   selecione `snes9x_libretro_android.so` e aguarde a conclusão. O aplicativo
   copia o arquivo para seu diretório de núcleos; não é necessário root.
5. Carregue Snes9x e sua ROM original. Se o arquivo não aparecer no seletor,
   confira **Configurações > Diretório > Downloads**: a pasta configurada pode
   ser diferente da pasta Download do Android.
6. Após instalar, considere bloquear o núcleo em **Gerenciar núcleos** para que
   o atualizador não o substitua pela versão original.

Os nomes dos menus podem variar conforme a versão e o idioma do RetroArch.

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

### Testes automatizados

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
- Frontends de opções 2/1/legado e definições em inglês/turco. Nesta revisão,
  frontends que anunciam versão 2 recebem a interface de opções v1 do upstream.

O build Android foi vinculado e passou na inspeção ELF: AArch64, API 21,
segmentos alinhados a 16 KB, 25 funções obrigatórias da API Libretro e
importações verificadas contra os stubs Android API 21. As dependências de
runtime são apenas `libc.so`, `libdl.so` e `libm.so`.

### Teste real no RetroArch Android

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

O [workflow Android ARM64](.github/workflows/android-arm64.yml) usa
**Android NDK r28c / 28.2.13676358**, `ndk-build`, `arm64-v8a`, `android-21`,
`c++_static`, suporte a páginas de 16 KB e link com `--no-undefined`.
Não é necessário inicializar os submódulos das interfaces desktop para este núcleo.

Para compilar em um host Linux x86_64 com Git, Make, Python 3 e o NDK instalado:

```sh
git clone https://github.com/VrepliroidV/snes9x-ntsc-lie-pal.git
cd snes9x-ntsc-lie-pal
git checkout feat/ntsc-lie-pal-android-arm64
export ANDROID_NDK_HOME=/caminho/android-ndk-r28c
bash scripts/build-android-arm64.sh
```

A saída fica em `build/android-arm64/`, incluindo o `.so`, checksums, metadados,
log, relatórios e licenças. O artefato de CI também inclui `android-core-smoke`,
um verificador opcional de carregamento; ele não faz parte do ZIP básico de
instalação e não foi relatado como executado no teste do aparelho.

Para reproduzir os testes de região no host:

```sh
bash ci/test-region.sh
```

Uma nova compilação pode produzir outro hash e identificar outro commit. A Beta 1
é preparada a partir do artefato aprovado da execução indicada; o processo de
empacotamento não recompila nem modifica seu `.so`. Sua origem e os hashes ficam
registrados nos arquivos do lançamento e nas
[notas preparadas da Beta 1](docs/releases/v1.0.0-beta.1.md).

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
