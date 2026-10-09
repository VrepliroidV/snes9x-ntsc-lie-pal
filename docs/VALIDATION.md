# Validação — Android ARM64

Registro da validação realizada em 09/10/2026, em um ambiente Linux x86_64.
Os resultados abaixo distinguem execução no host, inspeção do artefato Android
e testes ainda pendentes no dispositivo.

## Fonte e compilação

- Base oficial: `snes9xgit/snes9x`,
  `1bcc369e89f08243e0a462882fb1f3e42e51de3a`, Snes9x 1.63.
- A revisão contém `libretro/libretro.cpp`, `libretro/jni/Android.mk`,
  `Application.mk` e as fontes necessárias. Não foram necessários submódulos
  desktop ou atualização para uma revisão diferente.
- Compilação real com Android NDK r28c, `28.2.13676358`, `ndk-build`,
  `arm64-v8a`, Android API 21, configuração release e `c++_static`.
- Compilação e vinculação concluídas. O link exige `--no-undefined`.
  Consulte `build.log` e `build-diagnostics.json` no pacote para os diagnósticos
  exatos; o commit usado e o estado da árvore estão em `build-metadata.txt`.

## Inspeção do núcleo Android

O script `scripts/verify-android-core.py` confirmou no artefato produzido:

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
não distribuídas. **Não prova `dlopen` no Android, o carregamento pelo RetroArch
ou a execução de um jogo no aparelho.** O SHA256 e o tamanho exato do arquivo
estão em `SHA256SUMS` e `elf-validation.json`, evitando fixar aqui um hash que
mudaria após outra compilação.

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

O pacote inclui `android-core-smoke`, um executável Android ARM64/API21
compilado com o mesmo NDK. Ele foi **compilado, mas não executado no ambiente
de validação**. Com o aparelho conectado e a depuração USB autorizada, execute
na pasta extraída do pacote:

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

## Verificação pendente no RetroArch Android

1. Instale o núcleo conforme o README e confirme que o RetroArch é aarch64.
   Registre Android, modelo do aparelho, versão do RetroArch, commit e SHA256.
2. Use a ROM original europeia de Donkey Kong Country 2 (Europe Rev 1), sem
   patches, iniciando sem estado salvo. Este jogo **não foi executado aqui**.
3. Escolha `NTSC (Lie to PAL)`, salve as opções e recarregue núcleo/conteúdo.
   Verifique se passa pela checagem de região, inicia e joga com aproximadamente
   60,10 FPS. Observe áudio, sincronização, estabilidade e desempenho sustentado.
4. Compare Auto e PAL (~50,01 FPS), NTSC e o novo modo, sempre recarregando.
   Confirme também um jogo NTSC nos modos Auto e NTSC.
5. Faça reset e teste salvar/carregar estado no mesmo modo. Não reutilize um
   estado feito em PAL para validar o modo NTSC.
6. Preserve os logs do RetroArch em caso de falha. Se disponível, repita em
   aparelho com páginas de 16 KB; o alinhamento passou na inspeção estática,
   mas esse aparelho não foi testado.

Os testes prévios do usuário no Beetle Supafaust e em uma edição binária
experimental motivaram a implementação. Eles não são contabilizados como
validação deste núcleo compilado.

## GitHub Actions e plataformas futuras

O workflow está preparado para compilar, validar e publicar artifacts dos
pushes/PRs. A sintaxe foi validada com `actionlint` 1.7.12; os scripts passaram
em `bash -n` e na compilação do Python. Na sessão de validação, `git push`
retornou HTTP 403 e a API de criação
de branch retornou `Resource not accessible by integration`. A branch está
criada localmente; **nenhuma execução desse workflow nem PR remoto foi validado**
enquanto o acesso de gravação permanecer indisponível.

O build Android local e os testes acima foram realmente executados, apesar desse
bloqueio de publicação. Windows x64 e Linux ARM64/Rocknix permanecem fora do
escopo validado nesta etapa.
