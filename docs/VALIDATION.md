# Validação — Android ARM64

O registro inicial foi produzido em 09/10/2026, em um ambiente Linux x86_64.
Este documento também registra a execução bem-sucedida do GitHub Actions e o
teste no RetroArch Android relatado pelo usuário. Os resultados distinguem os
testes no host, a inspeção estática do artefato Android e a execução no aparelho.

## Fonte e compilação

- Base oficial: `snes9xgit/snes9x`,
  `1bcc369e89f08243e0a462882fb1f3e42e51de3a`, Snes9x 1.63.
- A revisão contém `libretro/libretro.cpp`, `libretro/jni/Android.mk`,
  `Application.mk` e as fontes necessárias. Não foram necessários submódulos
  desktop ou atualização para uma revisão diferente.
- Compilação real com Android NDK r28c, `28.2.13676358`, `ndk-build`,
  `arm64-v8a`, Android API 21, configuração release e `c++_static`.
- Compilação e vinculação concluídas. O link exige `--no-undefined`.
  Consulte `build.log` e `build-diagnostics.json` no artefato de CI para os diagnósticos
  exatos; o commit usado e o estado da árvore estão em `build-metadata.txt`.
- O build local inicial usou o commit
  `827fb71c638b2c0361d973e89c714b10bcd6a9b8`. A execução do GitHub Actions
  documentada abaixo usou `1cc6195d`. São compilações separadas; use os
  metadados e `SHA256SUMS` do pacote baixado para identificar seu núcleo.

## Inspeção do núcleo Android

O script `scripts/verify-android-core.py` confirmou no artefato local produzido:

- ELF64 little-endian, máquina AArch64, tipo ET_DYN.
- Nota Android indicando API 21 e NDK r28c.
- Segmentos LOAD alinhados a 16.384 bytes, com offsets congruentes; pilha sem
  permissão de execução e ausência de TEXTREL/RPATH/RUNPATH.
- Os 25 pontos de entrada obrigatórios da API Libretro estão exportados como
  funções. Há ainda duas variáveis `retro_*` exportadas pelo núcleo original.
- Dependências somente `libc.so`, `libdl.so` e `libm.so`.
  Não há dependência de `libc++_shared.so`, glibc ou biblioteca do host.
- Todos os símbolos importados fortes existem nos stubs Android AArch64/API21
  das dependências declaradas no NDK. Não há importações fracas neste artefato.

Isso reduz a chance de erros de arquitetura, símbolos ausentes e dependências
não distribuídas. **A inspeção estática, por si só, não prova o carregamento
no Android ou a execução de um jogo.** O teste no RetroArch relatado pelo
usuário está registrado separadamente abaixo. O SHA256 e o tamanho exato
do arquivo estão em `SHA256SUMS` de cada lançamento e em `elf-validation.json`
dos artefatos de CI.

## Testes funcionais executados no host

`ci/test-region.sh` compila a mesma interface Libretro em uma cópia isolada,
carrega a biblioteca com `dlopen(RTLD_NOW)` e executa uma ROM de diagnóstico
original gerada em memória por `tests/region_test.cpp`. Não utiliza ROM comercial,
patch ou símbolo privado do emulador.

A ROM executa instruções 65816 que leem `$213F` e os contadores de vídeo
`$2137/$213D`, deixando os resultados na WRAM. Assim, o teste observa o que a
CPU emulada recebe e a temporização efetivamente executada.

Foram verificados 72 cenários e 144 carregamentos de ROM:

- Cabeçalhos USA/NTSC e Europe/PAL, Auto/NTSC/PAL/`ntsc_lie_pal`, com repetição
  e alternância entre modos para detectar configuração residual.
- Frontends que anunciam opções versão 2, versão 1 e interface legada, em inglês
  e turco. Esta revisão responde aos frontends v2 com a interface v1 oficial.
- Quatro valores na ordem esperada, nome `NTSC (Lie to PAL)` e default `auto`.
- Bit 4 reportado corretamente em todos os modos.
- Maior contador vertical de 261 em NTSC e 311 em PAL, inclusive NTSC real
  quando o modo reporta PAL.
- `retro_get_region()` e FPS consistentes com os relógios oficiais:
  NTSC `21477272 / 357366` Hz; PAL `21281370 / 425568` Hz.
- Reset, serialização/restauração no mesmo modo e restauração real da WRAM.
- Mudança de região durante execução adiada para o próximo carregamento,
  e atualização correta após descarregar/carregar o conteúdo.

O resultado é registrado em `region-tests.log`. Esses testes executam emulação
no host x86_64; **não executam o binário Android ARM64**. A revisão independente
do código também verificou o caminho DMA reverso e a preservação dos outros
bits/efeitos de STAT78; estes aspectos não foram exercitados como testes de
hardware completos pela ROM de diagnóstico.

## Teste opcional de carregamento no Android

O artefato do GitHub Actions inclui `android-core-smoke`, um executável Android ARM64/API21
compilado com o mesmo NDK. Ele foi **compilado, mas não executado no ambiente
de validação**. Com o aparelho conectado e a depuração USB autorizada, execute
na pasta extraída do artefato de CI (o ZIP básico da Beta não inclui esse executável):

```sh
adb push snes9x_libretro_android.so /data/local/tmp/
adb push android-core-smoke /data/local/tmp/
adb shell chmod 755 /data/local/tmp/android-core-smoke
adb shell /data/local/tmp/android-core-smoke /data/local/tmp/snes9x_libretro_android.so
```

O teste usa `dlopen(RTLD_NOW)`, resolve os 25 símbolos, verifica a versão da
API Libretro e consulta as informações do núcleo. Se passar, imprime
`Android dlopen and 25 Libretro symbols passed` com a versão/commit.
Ele não inicializa a emulação nem carrega ROMs. Um resultado positivo ainda
precisa ser seguido pelo teste dentro do RetroArch.

## Teste relatado no RetroArch Android

O usuário informou ter testado o núcleo produzido pelo GitHub Actions no
RetroArch Android e confirmado que a opção **NTSC (Lie to PAL)** aparece e
funciona. Com a ROM original **Donkey Kong Country 2 (Europe) (Rev 1)**,
concluiu a primeira fase e desbloqueou a
segunda.

Esse relato demonstra carregamento do núcleo, acesso à nova opção e progresso
no jogo nesse cenário. O teste não foi reproduzido independentemente neste
ambiente e não garante compatibilidade com todos os jogos PAL. Não
foram informados modelo do aparelho, versão do Android, versão do RetroArch,
FPS medidos ou resultados específicos de áudio, estados salvos e desempenho
sustentado. A temporização NTSC foi verificada pelos testes automáticos no host;
não há uma medição de FPS do aparelho registrada aqui.

O executável `android-core-smoke` continua **compilado, mas sem execução
standalone registrada**. O teste do núcleo dentro do RetroArch não deve ser
confundido com uma execução desse diagnóstico separado.

## Verificações complementares no dispositivo

1. Instale o núcleo conforme o README e confirme que o RetroArch é aarch64.
   Registre Android, modelo do aparelho, versão do RetroArch, commit e SHA256.
2. Para repetir o teste relatado, use a ROM original europeia de Donkey Kong
   Country 2 (Europe Rev 1), sem patches, iniciando sem estado salvo. Continue
   a partir da segunda fase para ampliar a cobertura de jogo.
3. Escolha `NTSC (Lie to PAL)`, salve as opções e recarregue núcleo/conteúdo.
   Meça o FPS esperado de aproximadamente 60,10 Hz e observe áudio,
   sincronização, estabilidade e desempenho sustentado.
4. Compare Auto e PAL (~50,01 FPS), NTSC e o novo modo, sempre recarregando.
   Confirme também um jogo NTSC nos modos Auto e NTSC.
5. Faça reset e teste salvar/carregar estado no mesmo modo. Não reutilize um
   estado feito em PAL para validar o modo NTSC.
6. Preserve os logs do RetroArch em caso de falha. Se disponível, repita em
   aparelho com páginas de 16 KB; o alinhamento passou na inspeção estática,
   mas esse aparelho não foi testado.

Os testes prévios no Beetle Supafaust e em uma edição binária experimental
motivaram a implementação. Eles são distintos do teste posterior do usuário
com o núcleo compilado por este projeto, registrado acima.

## GitHub Actions e plataformas futuras

O workflow compilou e validou o núcleo na execução
[`37941390187`](https://github.com/VrepliroidV/snes9x-ntsc-lie-pal/actions/runs/37941390187),
com fonte no commit `1cc6195d`. Os jobs `android-arm64` e `region-regression`
terminaram com **success**. O primeiro produziu o núcleo Android e os relatórios
de validação; o segundo executou os 72 cenários e 144 carregamentos no host.
Os artifacts dessa execução têm retenção de 30 dias. Para identificar o binário
distribuído, consulte os metadados e o manifesto `SHA256SUMS` do pacote/release.

## Proveniência da Beta 1 preparada

O artefato Android aprovado foi baixado e seu ZIP conferido contra o digest
registrado pelo GitHub. O manifesto interno `SHA256SUMS` também passou para
o núcleo e para o diagnóstico de carregamento. Os dados são:

| Campo | Valor |
| --- | --- |
| Execução | `37941390187` |
| Artifact Android | `11621278803` |
| Fonte compilada | `1cc6195dc4a2449679ec7d47994748301a5d5ee6` |
| SHA256 do ZIP original de CI | `a64765706355d8d0b57b89f7b4b55bc4e30c3cd972d88fe3ba456a7399060eaf` |
| SHA256 de `snes9x_libretro_android.so` | `151093f5c13e4743f7ae52646a0faad2c4d04ab3c41d7f72a1d667e687bb5d44` |
| Diagnósticos do build de CI | Zero avisos e zero erros |

O binário da Beta é uma cópia desse `.so`. O binário local anterior, compilado
de `827fb71c`, tem outro hash e não é usado como substituto. Os ajustes de
documentação do lançamento não alteram a implementação. A publicação da tag
e do pre-release depende da revisão e confirmação do responsável pelo projeto.

O registro local anterior continua válido como histórico separado. A sintaxe
do workflow foi validada com `actionlint` 1.7.12; os scripts passaram em
`bash -n` e na compilação do Python. As falhas de acesso de gravação daquela
sessão não representam o resultado da execução do GitHub Actions acima.

Windows x64 e Linux ARM64/Rocknix permanecem fora do escopo validado nesta etapa.
