# Snes9x Libretro — NTSC (Lie to PAL)

Modificação do Snes9x Libretro para executar jogos com temporização NTSC enquanto
o bit de região lido pelo jogo informa PAL. O primeiro alvo é **Android ARM64
(arm64-v8a), Android 5.0/API 21 ou superior, com RetroArch de 64 bits**.

## Origem e implementação

A base é a árvore oficial de [snes9xgit/snes9x](https://github.com/snes9xgit/snes9x),
revisão [1bcc369e89f08243e0a462882fb1f3e42e51de3a](https://github.com/snes9xgit/snes9x/commit/1bcc369e89f08243e0a462882fb1f3e42e51de3a)
(Snes9x 1.63, 24/09/2026). Essa revisão contém a interface Libretro e os arquivos
NDK em `libretro/jni`. O commit de importação preserva a árvore e o histórico
oficial; as alterações deste projeto vêm depois dele. As licenças e os avisos
originais dos arquivos continuam aplicáveis.

`snes9x_region` oferece estas opções, nesta ordem:

| Opção | Valor salvo | Temporização | STAT78 ($213F), bit 4 |
| --- | --- | --- | --- |
| Auto | `auto` | Detectada pelo cabeçalho, como no original | Região detectada |
| NTSC | `ntsc` | NTSC, aproximadamente 60,10 Hz | 0 (NTSC) |
| PAL | `pal` | PAL, aproximadamente 50,01 Hz | 1 (PAL) |
| NTSC (Lie to PAL) | `ntsc_lie_pal` | NTSC, aproximadamente 60,10 Hz | 1 (PAL) |

- `Settings.PAL` continua controlando relógios, linhas por quadro, áudio,
  proporção automática e `retro_get_region()`.
- `Settings.NTSCLiePAL` é uma configuração separada, inicialmente falsa. No
  novo modo, `ForceNTSC=true` e `ForcePAL=false`; somente a leitura de `$213F`
  usa `Settings.PAL || Settings.NTSCLiePAL` para calcular o bit 4.
- Os demais bits, o barramento aberto e os efeitos da leitura de STAT78 são
  preservados. CPU e DMA reverso chegam à mesma função `S9xGetPPU`.
- A região reportada é aplicada ao carregar o conteúdo, inclusive pelo caminho
  `retro_load_game_special`. Alterar opções durante a execução não muda esse
  bit parcialmente: **salve a opção e recarregue o núcleo/conteúdo**.
- As listas de opções em inglês e turco, únicas traduções desta revisão, incluem
  o novo valor. Frontends com opções v1 ou fallback legado recebem o valor.

Nenhuma ROM comercial é modificada ou incluída. O funcionamento não depende
de IPS, UPS, BPS ou edição binária. A opção não garante compatibilidade com todos
os jogos PAL: alguns verificam outros aspectos do hardware ou assumem 50 Hz.

## Compilar Android ARM64

Em um host Linux x86_64, instale Git, Make, Python 3 e o **Android NDK r28c
(28.2.13676358)**. Não é necessário inicializar os submódulos das interfaces
desktop para este núcleo Libretro.

```sh
git clone https://github.com/VrepliroidV/snes9x-ntsc-lie-pal.git
cd snes9x-ntsc-lie-pal
git checkout feat/ntsc-lie-pal-android-arm64
export ANDROID_NDK_HOME=/caminho/android-ndk-r28c
bash scripts/build-android-arm64.sh
```

O script usa `ndk-build`, `APP_ABI=arm64-v8a`, `APP_PLATFORM=android-21`,
`APP_STL=c++_static` e suporte a páginas de 16 KB. Os arquivos ficam em
`build/android-arm64/`: `snes9x_libretro_android.so`, `SHA256SUMS`, relatório ELF,
metadados e log. O runtime C++ é vinculado estaticamente, evitando a necessidade
de instalar `libc++_shared.so` junto ao RetroArch.

O workflow [Android ARM64 core](.github/workflows/android-arm64.yml) executa em
push, Pull Request ou acionamento manual. Ele compila e valida o núcleo com o
mesmo NDK e executa os testes de região no host Linux. Depois de uma execução
bem-sucedida, abra a aba **Actions**, selecione a execução e baixe seu artifact
`snes9x-ntsc-lie-pal-android-arm64-<commit>`. Extraia o ZIP antes de instalar.

## Instalar no RetroArch Android

1. Use uma versão **aarch64** do [RetroArch oficial](https://docs.libretro.com/guides/install-android/).
   Um aparelho ARM64 também pode executar um RetroArch de 32 bits; esse processo
   não carrega um núcleo de 64 bits.
2. Faça backup do Snes9x existente em **Configurações > Núcleo > Gerenciar
   núcleos > Snes9x > Fazer cópia de segurança**. O arquivo usa o nome habitual
   `snes9x_libretro_android.so` e **substitui o núcleo Snes9x instalado**.
3. Copie o `.so` extraído para uma pasta acessível. No RetroArch, abra **Menu
   principal > Carregar núcleo > Instalar ou restaurar núcleo** e selecione o
   arquivo. Esse menu copia o núcleo para o diretório privado do aplicativo,
   sem root. Desbloqueie o núcleo existente antes da instalação, se necessário.
   Se o arquivo não aparecer, confira **Configurações > Diretório > Downloads**;
   a pasta configurada pode ser diferente da pasta Download do Android.
4. Carregue Snes9x e a ROM original. Em **Menu rápido > Opções do núcleo**, escolha
   **Console Region (Reload Core) > NTSC (Lie to PAL)** e salve as opções para
   esse jogo. Feche o conteúdo e recarregue o núcleo e o jogo para aplicar.
5. Em **Gerenciar núcleos**, bloqueie o núcleo modificado para impedir que uma
   atualização automática o substitua pela versão original.

Para verificar uma opção salva manualmente, o valor correspondente é:

```ini
snes9x_region = "ntsc_lie_pal"
```

Use inicialização limpa para o primeiro teste. Estados salvos devem ser criados
e carregados no **mesmo modo de região**: estados de outro modo podem restaurar
temporizações incompatíveis, uma limitação anterior a esta modificação.

## Validação e diagnóstico

Consulte [docs/VALIDATION.md](docs/VALIDATION.md) para os resultados realmente
obtidos e os testes que ainda dependem de aparelho. Para reproduzir os testes
funcionais com ROMs sintéticas originais:

```sh
bash ci/test-region.sh
```

O validador Android examina arquitetura, segmentos ELF, símbolos Libretro e
dependências. Isso não equivale a executar `dlopen` no Android ou jogar no
RetroArch. O pacote inclui um executável de verificação de carregamento, descrito
na documentação de validação, para uso opcional com `adb`.

Se o núcleo instalar mas não carregar, ative **Configurações > Registro/Logging
> Verbosidade e Registrar em arquivo** e consulte a pasta configurada de logs.
Veja também o [guia oficial de logs](https://docs.libretro.com/guides/generating-retroarch-logs/).

| Mensagem no log | O que verificar |
| --- | --- |
| `wrong ELF class`, `ELFCLASS64`, `unexpected e_machine` | RetroArch precisa ser aarch64; confirme que o arquivo é o artefato Android ARM64. |
| `libc++_shared.so not found` | O arquivo foi produzido com C++ dinâmico; este build usa `c++_static` e não exige essa biblioteca. |
| `cannot locate symbol` | Versão Android/API ou dependência incompatível; preserve o log completo e o SHA256 do núcleo. |
| `Permission denied` | Instale pelo menu do RetroArch para copiar o núcleo ao diretório privado do aplicativo. |
| Nova opção ausente | Confirme o núcleo e sua versão/commit em Informações do núcleo; recarregue-o e confira se o atualizador substituiu o arquivo. |

Windows x64 e Linux ARM64/Rocknix ainda não fazem parte da matriz de artefatos
validados deste projeto. O build Linux de teste serve para validar a emulação,
não como entrega dessas plataformas.

## Nightly builds

Download nightly builds from continuous integration:

### snes9x

| OS            | status                                           |
|---------------|--------------------------------------------------|
| Windows       | [![Status][s9x-win-all]][appveyor]               |
| Linux (GTK)   | [![Status][snes9x_linux-gtk-amd64]][cirrus-ci]   |
| Linux (X11)   | [![Status][snes9x_linux-x11-amd64]][cirrus-ci]   |
| FreeBSD (X11) | [![Status][snes9x_freebsd-x11-amd64]][cirrus-ci] |
| macOS         | [![Status][snes9x_macOS-amd64]][cirrus-ci]       |

[appveyor]: https://ci.appveyor.com/project/snes9x/snes9x
[cirrus-ci]: http://cirrus-ci.com/github/snes9xgit/snes9x

[s9x-win-all]: https://ci.appveyor.com/api/projects/status/github/snes9xgit/snes9x?branch=master&svg=true
[snes9x_linux-gtk-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=snes9x_linux-gtk-amd64
[snes9x_linux-x11-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=snes9x_linux-x11-amd64
[snes9x_freebsd-x11-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=snes9x_freebsd-x11-amd64
[snes9x_macOS-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=snes9x_macOS-amd64

### libretro core

| OS                  | status                                                  |
|---------------------|---------------------------------------------------------|
| Linux/amd64         | [![Status][libretro_linux-amd64]][cirrus-ci]            |
| Linux/i386          | [![Status][libretro_linux-i386]][cirrus-ci]             |
| Linux/armhf         | [![Status][libretro_linux-armhf]][cirrus-ci]            |
| Linux/armv7-neon-hf | [![Status][libretro_linux-armv7-neon-hf]][cirrus-ci]    |
| Linux/arm64         | [![Status][libretro_linux-arm64]][cirrus-ci]            |
| Android/arm         | [![Status][libretro_android-arm]][cirrus-ci]            |
| Android/arm64       | [![Status][libretro_android-arm64]][cirrus-ci]          |
| Emscripten          | [![Status][libretro_emscripten]][cirrus-ci]             |
| macOS/amd64         | [![Status][libretro_macOS-amd64]][cirrus-ci]            |
| Nintendo Wii        | [![Status][libretro_nintendo-wii]][cirrus-ci]           |
| Nintendo Switch     | [![Status][libretro_nintendo-switch-libnx]][cirrus-ci]  |
| Nintendo GameCube   | [![Status][libretro_nintendo-ngc]][cirrus-ci]           |
| PSP                 | [![Status][libretro_playstation-psp]][cirrus-ci]        |

[libretro_linux-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_linux-amd64
[libretro_linux-i386]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_linux-i386
[libretro_linux-armhf]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_linux-armhf
[libretro_linux-armv7-neon-hf]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_linux-armv7-neon-hf
[libretro_linux-arm64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_linux-arm64
[libretro_android-arm]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_android-arm
[libretro_android-arm64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_android-arm64
[libretro_emscripten]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_emscripten
[libretro_macOS-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_macOS-amd64
[libretro_nintendo-wii]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_nintendo-wii
[libretro_nintendo-switch-libnx]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_nintendo-switch-libnx
[libretro_nintendo-ngc]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_nintendo-ngc
[libretro_playstation-psp]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_playstation-psp
