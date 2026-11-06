#!/bin/bash
# Build Syphon Filter 3 PC (sf3_nativo).
#
#   ./build.sh linux        -> build/linux/sf3_nativo
#   ./build.sh win          -> build/win/sf3_nativo.exe  (cross-compiled with llvm-mingw)
#
# Requirements (paths can be overridden with environment variables):
#   SDL2_DIR        SDL2 development files (include/ and lib/). For Windows use the
#                   x86_64-w64-mingw32 folder of the SDL2 MinGW development package.
#   SDL2_INCLUDE    (Linux) SDL2 include folder if it is not SDL2_DIR/include.
#   LINUX_LIBS      (Linux) libraries to link, default "-lSDL2 -lGL -lpthread -ldl".
#   OPENAL_INCLUDE  OpenAL Soft include folder (headers only; used by PsyCross).
#   LLVM_MINGW      llvm-mingw toolchain (only for "win").
#   python3         to embed data/recursos.bin.
set -e
T=${1:-linux}
ROOT=$(cd "$(dirname "$0")" && pwd)
OUT=$ROOT/build/$T; OBJ=$OUT/obj; mkdir -p $OBJ/psycross $OBJ/gen
OALI=${OPENAL_INCLUDE:-/usr/include}
if [ $T = win ]; then
  export PATH=${LLVM_MINGW:-/opt/llvm-mingw}/bin:$PATH
  CC=${CC:-x86_64-w64-mingw32-clang}; CXX=${CXX:-x86_64-w64-mingw32-clang++}; AR=${AR:-x86_64-w64-mingw32-ar}
  SDLD=${SDL2_DIR:-/opt/SDL2/x86_64-w64-mingw32}; SDLI=$SDLD/include
  LIBS="-L$SDLD/lib -lmingw32 -lSDL2main -lSDL2 -lopengl32 -lwinmm -lgdi32 -luser32 -lshell32 -lcomctl32 -ldbghelp -static-libgcc -static-libstdc++ -Wl,-Bstatic -lc++ -lunwind -lwinpthread -Wl,-Bdynamic"
  EXE=sf3_nativo.exe; EXTRA="-mwindows"; PSYINC="-include windows.h"
else
  CC=${CC:-clang}; CXX=${CXX:-clang++}; AR=${AR:-ar}
  SDLI=${SDL2_INCLUDE:-${SDL2_DIR:-/usr}/include}
  LIBS=${LINUX_LIBS:-"-lSDL2 -lGL -lpthread -ldl"}
  EXE=sf3_nativo; EXTRA=""; PSYINC="-include /usr/include/strings.h"
fi

# 1. PsyCross (third_party, MIT)
P=$ROOT/third_party/PsyCross
for f in $P/src/render/glad.c $P/src/util/timer.c $P/src/psx/*.C $P/src/audio/*.cpp $P/src/gpu/*.cpp $P/src/gte/*.cpp $P/src/pad/*.cpp $P/src/render/PsyX_render.cpp $P/src/util/crash_handler.cpp $P/src/PsyX_main.cpp; do
  o=$OBJ/psycross/$(basename $f).o
  [ $o -nt $f ] && continue
  case $f in
    *.c|*.C) $CC -x c -std=c2x -w -O2 -include $P/fwd.h -c $f -I$P/include -I$P/include/psx -I$P/src -I$SDLI -I$SDLI/SDL2 -I$OALI -I$OALI/AL -o $o ;;
    */PsyX_main.cpp) $CXX -std=c++20 -w -O2 $PSYINC -c $f -I$P/include -I$P/include/psx -I$P/src -I$SDLI -I$SDLI/SDL2 -I$OALI -I$OALI/AL -o $o ;;
    *) $CXX -std=c++20 -w -O2 -c $f -I$P/include -I$P/include/psx -I$P/src -I$SDLI -I$SDLI/SDL2 -I$OALI -I$OALI/AL -o $o ;;
  esac
done
rm -f $OBJ/libpsycross.a; $AR rcs $OBJ/libpsycross.a $OBJ/psycross/*.o

# 2. Embedded resources
python3 $ROOT/tools/recursos.py inc $ROOT/data/recursos.bin $OBJ/gen/recursos_blob.inc

# 3. Engine and application
FL="-std=c++23 -O2 -w -DSF3_RELEASE -I$OBJ/gen -I$ROOT/include -I$ROOT/apps/sf3_nativo -I$P/include -I$P/include/psx -I$SDLI -I$SDLI/SDL2 -I$OALI"
objs=""
for f in $ROOT/src/core/*.cpp $ROOT/src/disc/*.cpp $ROOT/src/psx/*.cpp $ROOT/src/assets/*.cpp; do
  o=$OBJ/$(basename $f).o; objs="$objs $o"
  [ $o -nt $f ] || $CXX $FL -c $f -o $o
done
$CXX $FL -c $ROOT/apps/sf3_nativo/main.cpp -o $OBJ/main.o
RES=""
if [ $T = win ]; then (cd $ROOT/apps/sf3_nativo && x86_64-w64-mingw32-windres sf3_nativo.rc -O coff -o $OBJ/res.o); RES=$OBJ/res.o; fi
$CXX $EXTRA -o $OUT/$EXE $OBJ/main.o $RES $objs $OBJ/libpsycross.a $LIBS
echo "built $OUT/$EXE"
