#!/usr/bin/env bash
# Group Vulkan validation-layer messages by VUID identifier.
# Mirrors scripts/mac/ubsan-group.sh in spirit and output format.
#
# Usage: vk-validation-group.sh <validation.log>
#
# Output:
#   <log>.grouped.md      markdown report
#   stdout                same report (for piping)

set -eu

LOG="${1:-}"
[ -n "$LOG" ] && [ -f "$LOG" ] || { echo "usage: $0 <log>" >&2; exit 2; }

TRIAGE_CFG="$(dirname "$0")/vk-triage.cfg"
[ -f "$TRIAGE_CFG" ] || { echo "ERROR: triage config not found: $TRIAGE_CFG" >&2; echo "without it every VUID would fall through to 'unclassified'; refusing to proceed silently." >&2; exit 3; }

OUT="${LOG}.grouped.md"

awk -v out="$OUT" -v cfg="$TRIAGE_CFG" '
BEGIN {
  # Load triage rules: severity | vuid-glob | note
  n_rules = 0
  if ((getline line < cfg) > 0) {
    do {
      sub(/[ \t]*#.*$/, "", line)
      if (line ~ /^[ \t]*$/) { continue }
      split(line, fld, /[ \t]*\|[ \t]*/)
      if (fld[1] && fld[2]) {
        n_rules++
        rule_sev[n_rules] = fld[1]
        rule_pat[n_rules] = fld[2]
        rule_note[n_rules] = fld[3]
      }
    } while ((getline line < cfg) > 0)
    close(cfg)
  }
}

# match VUID-... or UNASSIGNED-... tokens
function find_vuid(s,    re, m) {
  re = "(VUID-[A-Za-z0-9_-]+|UNASSIGNED-[A-Za-z0-9_-]+)"
  if (match(s, re)) {
    return substr(s, RSTART, RLENGTH)
  }
  return ""
}
function glob_match(text, pat,    re) {
  re = pat
  gsub(/\./, "\\.", re)
  gsub(/\*/, ".*", re)
  return text ~ ("^" re "$")
}
function severity_for(vuid,    i) {
  for (i = 1; i <= n_rules; i++) {
    if (glob_match(vuid, rule_pat[i])) { return rule_sev[i] }
  }
  return "unclassified"
}

{
  v = find_vuid($0)
  if (v == "") {
    # collect non-VUID stderr noise (loader warnings, MVK warnings) into a bucket
    if ($0 ~ /MoltenVK|MVK_|loader|Vulkan/) {
      bucket = "(no-VUID) " substr($0, 1, 80)
      counts[bucket]++
      first[bucket] = (first[bucket] == "" ? $0 : first[bucket])
    }
    next
  }
  counts[v]++
  first[v] = (first[v] == "" ? $0 : first[v])
  sev = severity_for(v)
  sev_of[v] = sev
}

END {
  sev_order["fatal"] = 1
  sev_order["high"] = 2
  sev_order["medium"] = 3
  sev_order["low"] = 4
  sev_order["unclassified"] = 5
  sev_order["ignore"] = 6

  # bucket VUIDs by severity
  for (v in counts) {
    s = (sev_of[v] != "" ? sev_of[v] : "unclassified")
    if (v ~ /^\(no-VUID\)/) { s = "unclassified" }
    if (!(s in any_for_sev)) any_for_sev[s] = ""
    any_for_sev[s] = any_for_sev[s] v "\n"
  }

  printf("# Vulkan validation triage: %s\n\n", FILENAME) > out
  printf("total grouped: %d unique signatures\n\n", length(counts)) > out

  # emit in severity order
  for (rank = 1; rank <= 6; rank++) {
    for (sn in sev_order) if (sev_order[sn] == rank) {
      if (!(sn in any_for_sev)) continue
      printf("## %s\n\n", toupper(sn)) > out
      n = split(any_for_sev[sn], vlist, "\n")
      for (i = 1; i <= n; i++) {
        v = vlist[i]; if (v == "") continue
        printf("- **`%s`** × %d\n", v, counts[v]) > out
        printf("  - sample: `%s`\n", first[v]) > out
      }
      printf("\n") > out
    }
  }
  close(out)
}
' "$LOG"

cat "$OUT"
echo ""
echo "report: $OUT"
