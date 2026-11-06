# Syphon Filter 3 PC

[English](README.md) | Español | [Русский](README_RU.md)

Versión 0.9.0, Windows 10/11 de 64 bits.

Este proyecto ejecuta el Syphon Filter 3 original de PlayStation (el ejecutable `SCUS_946.40` del disco USA) y lo dibuja con un renderizador nativo de PC. La lógica del juego no se toca: la IA, las misiones, el combate y las animaciones son las del juego. Lo que cambia es la imagen, los controles y algunos añadidos.

Hace falta tu propia copia del juego (disco USA en `.bin` + `.cue`). Aquí no se incluye ningún archivo del juego.

## Características

- Resolución interna libre, antialiasing MSAA/SMAA y pantalla ancha Hor+.
- PGXP: sin vértices temblando ni texturas deformadas, con z-buffer real.
- El juego mantiene su velocidad original en su propio hilo y la imagen se interpola a la frecuencia del monitor.
- Mira AU300 rehecha. La vista de rayos X ahora dibuja la escena real con la misma cámara, así que ve a través de las paredes, coincide con la vista exterior, no tiene retraso y pinta a los personajes y sus armas en verde como en la PS1.
- Iluminación del escenario, fogonazos que iluminan la habitación, humo, sangre, cristales, chispas, marcas de bala, nieve y vaho en las misiones frías, y visión nocturna como el original.
- Ratón 1:1 para apuntar, teclado y mando configurables, segundo jugador para el modo de 2 jugadores.
- Banco de armas: cualquier arma se puede usar en cualquier misión con su modelo, foto y sonido (truco Todas las armas). Se añadieron armas de Syphon Filter 1 y 2: lanzallamas, ametralladora Gatling, táser de mano y escopeta Stryker.
- El idioma del texto y el de las voces se eligen por separado: inglés (original), español (textos, voces y audio de los vídeos) y ruso (textos y doblaje).
- Lanzador de Windows, menú de opciones del PC (F12), tarjeta de memoria guardada como archivos, vídeo del menú principal en alta calidad, personajes añadidos al minijuego y al modo de 2 jugadores.

La lista completa está en [CARACTERISTICAS.txt](CARACTERISTICAS.txt).

## Cómo jugar

1. Pon el disco USA (`.bin` + `.cue`) en una carpeta, por ejemplo `SYPHON3\`.
2. Copia este proyecto dentro, por ejemplo `SYPHON3\SF3_PC\`.
3. Abre `bin\sf3_nativo.exe`. Busca el disco en su carpeta y en las de arriba.

La resolución, el idioma, los controles y lo demás se configuran en el lanzador. En la partida, F12 abre las opciones del PC (ahí están también los trucos: desbloquear todo, invulnerable, todas las armas, invisible), Esc abre el menú de pausa del juego y Alt+Enter cambia a pantalla completa.

## Datos opcionales

Algunas funciones necesitan datos que no se distribuyen en este repositorio. Van junto al exe o en una de las tres carpetas de arriba.

| Archivo | Contenido |
|---|---|
| `idiomas.pak` | Textos y voces en español y ruso, audio en español de los vídeos |
| `armas.pak` | Modelos, texturas, iconos, fotos, sonidos y configuración (`armas.txt`) de las armas extra |
| `personajes\` | Personajes añadidos al minijuego y al modo de 2 jugadores |

Si tienes los archivos sueltos (`textos_es.pak`, `voces_es.pak`, `videos_es_*.pak`, `voces_ru_*.pak` y la carpeta `armas`), `bin\CREAR_PAKS.bat` los junta en `idiomas.pak` y `armas.pak`. Un archivo suelto con el mismo nombre siempre tiene prioridad sobre el del pak.

## Carpetas

```
bin/                 listo para jugar: sf3_nativo.exe, SDL2.dll y utilidades
source/
  apps/sf3_nativo/   ventana, renderizador, menús, lanzador
  src/, include/     emulación del R3000, GPU/SPU/CD, disco y formatos
  third_party/       PsyCross, pl_mpeg, stb_vorbis
  data/recursos.bin  imágenes y sonidos del menú, comprimidos
  build.sh           script de compilación
```

## Compilar

Se compila con Clang. Para Windows se usa [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), que también funciona desde Linux:

```bash
cd source
SDL2_DIR=/ruta/SDL2/x86_64-w64-mingw32 \
OPENAL_INCLUDE=/ruta/openal-soft/include \
LLVM_MINGW=/ruta/llvm-mingw \
./build.sh win
```

Hacen falta los archivos de desarrollo de SDL2, las cabeceras de OpenAL Soft (las usa PsyCross) y Python 3 (para incrustar `data/recursos.bin`). `./build.sh linux` compila la versión de Linux.

## Créditos

- PsyCross del REDRIVER2 Project (MIT), base del renderizador de PC
- pl_mpeg de Dominic Szablewski (MIT), vídeo del menú
- stb_vorbis de Sean Barrett (dominio público / MIT)
- SDL2 (zlib)

Syphon Filter es una marca de Sony Interactive Entertainment. Este es un proyecto de aficionados sin relación con Sony ni con Bend Studio, y no incluye ningún archivo del juego original.
