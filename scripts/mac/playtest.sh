#!/usr/bin/env bash
# RealRTCW playtest launcher with ASAN/UBSAN capture.
#
# Запускает текущий arm64 билд, ловит любые ASAN/UBSAN reports в /tmp,
# и в конце выводит итоговый exit-код + summary санитайзеров.
#
# Флаги:
#   --vulkan     запустить под Vulkan-рендерером (+set cl_renderer vulkan).
#                По умолчанию OpenGL (cl_renderer=opengl1).
#   --auto       автотест: после старта +wait 600 (≈10s @ 60fps) +quit.
#                Нужен для headless-capture Vulkan validation триажа.
#   --           всё после — passthrough в RealRTCW.arm64.
#   <extra>      любые другие args (не --vulkan/--auto/--) тоже passthrough,
#                идут в командной строке ПЕРЕД +wait/+quit чтобы --auto не
#                съел их раньше времени.
#
# Если билд собран с -fsanitize (наличие Makefile.local с -fsanitize=...),
# инструментация активна — играй спокойно, при любом UAF/UB будет вопль
# в stderr ИЛИ в /tmp/rtcw-asan.<pid>.log.
#
# Если хочешь обычный (быстрый, без оверхеда) билд:
#   rm Makefile.local
#   make ARCH=arm64 USE_INTERNAL_LIBS=0 USE_OPENAL=0 -j8
#
# Если хочешь снова ASAN — Makefile.local восстанавливается одной строкой:
#   echo "BASE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1\nLDFLAGS += -fsanitize=address,undefined" > Makefile.local
#   make clean && make ARCH=arm64 USE_INTERNAL_LIBS=0 USE_OPENAL=0 -j8

set -uo pipefail

VULKAN_MODE=0
AUTO_MODE=0
EXTRA_ARGS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --vulkan) VULKAN_MODE=1; shift;;
    --auto)   AUTO_MODE=1;   shift;;
    --)       shift; EXTRA_ARGS+=("$@"); break;;
    *)        EXTRA_ARGS+=("$1"); shift;;
  esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$REPO_ROOT/build/release-darwin-arm64-nosteam/RealRTCW.arm64"
ASAN_LOG_PREFIX="/tmp/rtcw-asan"
UBSAN_LOG_PREFIX="/tmp/rtcw-ubsan"

if [ ! -x "$BIN" ]; then
  echo "ERROR: бинарник не найден: $BIN"
  echo "       собери его: cd $REPO_ROOT && make ARCH=arm64 USE_INTERNAL_LIBS=0 USE_OPENAL=0 -j8"
  exit 1
fi

# Чистим прошлые логи sanitizer'а — чтобы видеть свежие.
rm -f "${ASAN_LOG_PREFIX}".* "${UBSAN_LOG_PREFIX}".* 2>/dev/null

# Определяем, есть ли ASAN в бинаре, чтобы предупредить
if otool -L "$BIN" 2>/dev/null | grep -q asan; then
  echo "═══ RealRTCW ASAN/UBSAN build ═══"
  echo "Sanitizer активен — будет медленнее обычного, но любой UAF/UB будет пойман."
else
  echo "═══ RealRTCW release build (без sanitizer'а) ═══"
fi
echo "Лог-префиксы:  ${ASAN_LOG_PREFIX}.<pid>.log   ${UBSAN_LOG_PREFIX}.<pid>.log"
echo "Бинарник:      $BIN"
echo ""

export ASAN_OPTIONS="abort_on_error=0:detect_leaks=0:halt_on_error=0:log_path=${ASAN_LOG_PREFIX}"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0:log_path=${UBSAN_LOG_PREFIX}"

# cd в каталог с бинарником, чтобы он нашёл renderer .dylib рядом.
cd "$(dirname "$BIN")"
# stdin → /dev/null: иначе Q3-движок включает встроенную TTY-консоль и
# крадёт клавиатуру у SDL-окна (символы дублируются в терминал).
RENDERER_ARGS=()
if [ "$VULKAN_MODE" = "1" ]; then
  RENDERER_ARGS+=("+set" "cl_renderer" "vulkan")
  echo "Renderer:      Vulkan (cl_renderer=vulkan)"
else
  # cl_renderer is CVAR_ARCHIVE — persists in realrtcw_cvars.cfg from
  # whichever value the previous run left there. Without an explicit
  # override here, the OpenGL "baseline" run would silently load the
  # vendored Vulkan DLL because the cfg still says cl_renderer=vulkan.
  # Force opengl1 to make this script's "Renderer:" line truthful.
  RENDERER_ARGS+=("+set" "cl_renderer" "opengl1")
  echo "Renderer:      OpenGL (cl_renderer=opengl1, forced)"
fi

AUTO_ARGS=()
if [ "$AUTO_MODE" = "1" ]; then
  AUTO_ARGS+=("+wait" "600" "+quit")
  echo "Auto mode:     boot, 10s, quit"
  # Stale PID file from a Ctrl+C'd prior run would trigger the modal
  # SDL "abnormal exit / use safe video settings?" dialog in
  # Sys_InitPIDFile (code/sys/sys_main.c). With stdin redirected to
  # /dev/null the dialog blocks forever. Headless runs must wipe it.
  rm -f "$HOME/Library/Application Support/RealRTCW/main/iowolfsp.pid"
fi

"$BIN" "${RENDERER_ARGS[@]+"${RENDERER_ARGS[@]}"}" "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}" "${AUTO_ARGS[@]+"${AUTO_ARGS[@]}"}" < /dev/null
GAME_EXIT=$?

echo ""
echo "═══ Игра вышла с кодом $GAME_EXIT ═══"

ASAN_HITS=$(ls "${ASAN_LOG_PREFIX}".* 2>/dev/null | wc -l | tr -d ' ')
UBSAN_HITS=$(ls "${UBSAN_LOG_PREFIX}".* 2>/dev/null | wc -l | tr -d ' ')

if [ "$ASAN_HITS" -gt 0 ] || [ "$UBSAN_HITS" -gt 0 ]; then
  echo ""
  echo "⚠  Sanitizer что-то нашёл:"
  for f in ${ASAN_LOG_PREFIX}.* ${UBSAN_LOG_PREFIX}.*; do
    [ -f "$f" ] || continue
    echo "─── $f ───"
    head -40 "$f"
    echo "(см. полностью: cat $f)"
    echo ""
  done
else
  echo "✓ Никаких ASAN/UBSAN репортов. Sanitizer молчал — это хорошо."
fi

