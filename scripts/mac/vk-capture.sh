#!/usr/bin/env bash
# Run RealRTCW (Vulkan build) with validation layers + MoltenVK debug enabled,
# capturing all stderr to a timestamped log under /tmp/.
#
# Designed to be drop-in compatible with scripts/mac/playtest.sh from RealRTCW;
# when invoked with --auto it forwards to that script.
#
# Usage:
#   vk-capture.sh                  # full run, interactive
#   vk-capture.sh --auto           # use playtest.sh autotest mode if available
#   vk-capture.sh --map escape1    # spawn into specific map
#   vk-capture.sh --mvk-verbose    # MVK_CONFIG_LOG_LEVEL=3 (debug detail)
#   vk-capture.sh -- <extra args>  # pass extra args to the game binary

set -eu

# --- discover Vulkan layer paths -------------------------------------------
discover_layer_path() {
  local candidates=(
    "${VK_LAYER_PATH:-}"
    "${VULKAN_SDK:+$VULKAN_SDK/share/vulkan/explicit_layer.d}"
    "/opt/homebrew/share/vulkan/explicit_layer.d"
    "/usr/local/share/vulkan/explicit_layer.d"
    "$HOME/VulkanSDK/*/macOS/share/vulkan/explicit_layer.d"
  )
  for c in "${candidates[@]}"; do
    [ -n "$c" ] || continue
    # shellcheck disable=SC2086
    for expanded in $c; do
      if [ -d "$expanded" ] && ls "$expanded"/VkLayer_khronos_validation*.json >/dev/null 2>&1; then
        echo "$expanded"
        return 0
      fi
    done
  done
  return 1
}

discover_icd_path() {
  local candidates=(
    "${VK_ICD_FILENAMES:-}"
    "${VULKAN_SDK:+$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json}"
    "/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json"
    "/usr/local/share/vulkan/icd.d/MoltenVK_icd.json"
    "$HOME/VulkanSDK/*/macOS/share/vulkan/icd.d/MoltenVK_icd.json"
  )
  for c in "${candidates[@]}"; do
    [ -n "$c" ] || continue
    # shellcheck disable=SC2086
    for expanded in $c; do
      if [ -f "$expanded" ]; then
        echo "$expanded"
        return 0
      fi
    done
  done
  return 1
}

# --- parse args -------------------------------------------------------------
AUTO=0
MAP=""
MVK_VERBOSE=0
PASSTHRU=()

while [ $# -gt 0 ]; do
  case "$1" in
    --auto) AUTO=1; shift;;
    --map) MAP="$2"; shift 2;;
    --mvk-verbose) MVK_VERBOSE=1; shift;;
    --) shift; PASSTHRU+=("$@"); break;;
    -h|--help)
      sed -n '2,15p' "$0" | sed 's/^# \?//'
      exit 0;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done

LAYER_PATH="$(discover_layer_path || true)"
ICD_PATH="$(discover_icd_path || true)"

if [ -z "$LAYER_PATH" ]; then
  echo "ERROR: VK_LAYER_KHRONOS_validation not found." >&2
  echo "Install via: brew install vulkan-validationlayers" >&2
  echo "Or set VK_LAYER_PATH to the dir containing VkLayer_khronos_validation.json" >&2
  exit 1
fi
if [ -z "$ICD_PATH" ]; then
  echo "ERROR: MoltenVK ICD JSON not found." >&2
  echo "Install via: brew install molten-vk" >&2
  exit 1
fi

LOG="/tmp/vk-validation-$(date +%Y%m%d-%H%M%S).log"
echo "capture: $LOG"
echo "  layers:      $LAYER_PATH"
echo "  icd:         $ICD_PATH"
echo "  mvk-verbose: $MVK_VERBOSE"

export VK_ICD_FILENAMES="$ICD_PATH"
export VK_LAYER_PATH="$LAYER_PATH"
export VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"
export VK_LOADER_DEBUG="${VK_LOADER_DEBUG:-warn}"
export MVK_DEBUG=1
if [ "$MVK_VERBOSE" = "1" ]; then
  export MVK_CONFIG_LOG_LEVEL=3
fi

# --- delegate to playtest.sh if present ------------------------------------
# Must be invoked from the RealRTCW repo root; playtest.sh is CWD-relative.
PLAYTEST="scripts/mac/playtest.sh"
[ -x "$PLAYTEST" ] || PLAYTEST=""

if [ -n "$PLAYTEST" ]; then
  echo "delegating to $PLAYTEST"
  ARGS=()
  [ "$AUTO" = "1" ] && ARGS+=("--auto")
  [ -n "$MAP" ] && ARGS+=("--map" "$MAP")
  ARGS+=("${PASSTHRU[@]+"${PASSTHRU[@]}"}")
  "$PLAYTEST" "${ARGS[@]}" 2> >(tee -a "$LOG" >&2)
else
  echo "ERROR: $PLAYTEST not found; run vk-capture.sh from a RealRTCW checkout" >&2
  echo "or pass binary path via PASSTHRU after --" >&2
  exit 1
fi

echo ""
echo "log: $LOG"
echo "next: vk-validation-group.sh $LOG"
