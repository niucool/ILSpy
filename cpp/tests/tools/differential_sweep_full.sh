#!/bin/bash
# The full-corpus differential sweep (the port-disassembler branch's Phase-6
# completion follow-up): the C++ port's ilspy_cli vs the C# ilspycmd 11.0
# oracle over the FULL corpora --
#   * the 133 top-level net48 reference assemblies
#     (Microsoft.NETFramework.ReferenceAssemblies.net48 1.0.3,
#     /home/jim/ilspy-test-fixtures/net48 -- metadata-only, no method bodies)
#   * the 104 Facades/*.dll reference assemblies of the same set
#   * the 49 capa-testfiles .NET samples (the BSJB-marked files)
# in --il and --csharp (default) modes, 2 x 2 = 2 runs per sample.
#
# Extends the baml differential_harness.sh pattern (the same verdict
# taxonomy: IDENTICAL / DIFFERENT / PORT-CRASH(rc) / PORT-FAIL(rc) /
# ORACLE-FAIL-ONLY / BOTH-FAIL[-IDENTICAL] / TIMEOUT) with:
#   * the full corpora (the baml run sampled 15 of the net48 set)
#   * a crash-signature column: for every non-zero port exit, the first
#     meaningful line of the port's stderr (or stdout) -- the signature the
#     triage table dedupes on
#   * the port-fail message column (the port's stdout when it fails softly,
#     e.g. "no method bodies found" over the metadata-only reference
#     assemblies)
#
# Usage: differential_sweep_full.sh [output-dir]
#   ILSPY_ORACLE   the ilspycmd path (default ~/.dotnet/tools/ilspycmd)
#   ILSPY_PORT     the port's CLI (default: this repo's linux-ninja build)
#   CORPUS_DIR     the net48 corpus dir (default the fixture install)
#   CAPA_DIR       the capa sample dir (default /home/jim/source/capa-testfiles)
#   TIMEOUT        per-run timeout in seconds (default 120)

set -u

REPO=$(cd "$(dirname "$0")/../.." && pwd)
PORT=${ILSPY_PORT:-"$REPO/build/linux-ninja/ILSpyCmd/ilspy_cli"}
ORACLE=${ILSPY_ORACLE:-$HOME/.dotnet/tools/ilspycmd}
export DOTNET_ROOT=${DOTNET_ROOT:-/home/jim/.dotnet}
CORPUS_DIR=${CORPUS_DIR:-/home/jim/ilspy-test-fixtures/net48}
CAPA_DIR=${CAPA_DIR:-/home/jim/source/capa-testfiles}
OUT=${1:-/tmp/diffval/full}
TIMEOUT=${TIMEOUT:-120}

run_one() {  # $1=sample path  $2=tag  $3=port flags  $4=oracle flags
  local sample="$1" tag="$2" pflags="$3" oflags="$4"
  local dir="$OUT/$tag"
  mkdir -p "$dir"
  timeout "$TIMEOUT" "$ORACLE" $oflags "$sample" > "$dir/oracle.txt" 2> "$dir/oracle.err"
  echo $? > "$dir/oracle.rc"
  timeout "$TIMEOUT" "$PORT" $pflags "$sample" > "$dir/port.txt" 2> "$dir/port.err"
  echo $? > "$dir/port.rc"
  tr -d '\r' < "$dir/oracle.txt" > "$dir/oracle.n.txt"
  tr -d '\r' < "$dir/port.txt" > "$dir/port.n.txt"
  diff "$dir/oracle.n.txt" "$dir/port.n.txt" > "$dir/diff.txt"
  echo $? > "$dir/diff.rc"
}

# The crash/fail signature: the first meaningful line of the port's stderr,
# falling back to the port's stdout (the soft-fail messages print there).
port_signature() {  # $1=tag
  local dir="$OUT/$1"
  local line=""
  line=$(grep -m1 -E "Assertion|Segmentation|terminate|Aborted|what\(\)|Exception|error:|panic|Fatal|pure virtual" "$dir/port.err" 2>/dev/null)
  if [ -z "$line" ]; then
    line=$(head -1 "$dir/port.err" 2>/dev/null)
  fi
  if [ -z "$line" ]; then
    line=$(grep -m1 -E "no method bodies|error|Error" "$dir/port.n.txt" 2>/dev/null)
  fi
  if [ -z "$line" ]; then
    line=$(head -1 "$dir/port.n.txt" 2>/dev/null)
  fi
  echo "${line:0:140}" | tr '\t' ' '
}

emit_row() {  # $1=tag  $2=sample-display-name
  local tag="$1" sample="$2"
  local dir="$OUT/$tag"
  local orc rc
  orc=$(cat "$dir/oracle.rc"); rc=$(cat "$dir/port.rc")
  local olines plines dlines=0
  olines=$(wc -l < "$dir/oracle.n.txt")
  plines=$(wc -l < "$dir/port.n.txt")
  if [ "$(cat "$dir/diff.rc")" != "0" ]; then
    dlines=$(grep -cE "^[<>]" "$dir/diff.txt")
  fi
  local verdict=DIFFERENT
  if [ "$orc" = "124" ] || [ "$rc" = "124" ]; then
    verdict=TIMEOUT
  elif [ "$orc" != "0" ] && [ "$rc" != "0" ]; then
    if [ "$(cat "$dir/diff.rc")" = "0" ]; then
      verdict=BOTH-FAIL-IDENTICAL
    else
      verdict=BOTH-FAIL
    fi
  elif [ "$orc" != "0" ]; then
    verdict=ORACLE-FAIL-ONLY
  elif [ "$rc" = "134" ] || [ "$rc" = "139" ] || [ "$rc" = "136" ] || [ "$rc" = "138" ]; then
    verdict="PORT-CRASH($rc)"
  elif [ "$rc" != "0" ]; then
    verdict="PORT-FAIL($rc)"
  elif [ "$(cat "$dir/diff.rc")" = "0" ]; then
    verdict=IDENTICAL
  elif [ "$plines" = "1" ] && grep -q "no method bodies" "$dir/port.n.txt" 2>/dev/null; then
    verdict="PORT-FAIL-MSG"
  fi
  local sig="-"
  case "$verdict" in
    PORT-CRASH*|PORT-FAIL*|PORT-FAIL-MSG|TIMEOUT|ORACLE-FAIL-ONLY|BOTH-FAIL*)
      sig=$(port_signature "$tag") ;;
  esac
  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "$tag" "$verdict" "$orc" "$rc" \
    "$olines" "$plines" "$dlines" "$sig" "$sample" >> "$OUT/summary.tsv"
  echo "$tag: $verdict (oracle rc=$orc, port rc=$rc, lines o=$olines p=$plines, diff-lines=$dlines) sig: $sig"
}

mkdir -p "$OUT"
printf "tag\tverdict\toracle_rc\tport_rc\toracle_lines\tport_lines\tdiff_lines\tsignature\tsample\n" > "$OUT/summary.tsv"

i=0
for f in "$CAPA_DIR"/*; do
  [ -f "$f" ] || continue
  grep -qa BSJB "$f" 2>/dev/null || continue
  i=$((i+1))
  tag="capa$(printf %02d $i)_$(basename "$f" | cut -c1-10)"
  echo "--- $tag ($(basename "$f"))"
  run_one "$f" "${tag}_il" "--il" "-il"
  emit_row "${tag}_il" "$(basename "$f")"
  run_one "$f" "${tag}_cs" "--csharp" ""
  emit_row "${tag}_cs" "$(basename "$f")"
done

j=0
for f in "$CORPUS_DIR"/*.dll; do
  [ -f "$f" ] || continue
  j=$((j+1))
  tag="net$(printf %03d $j)_$(basename "$f" .dll | cut -c1-16)"
  echo "--- $tag ($(basename "$f"))"
  run_one "$f" "${tag}_il" "--il" "-il"
  emit_row "${tag}_il" "$(basename "$f")"
  run_one "$f" "${tag}_cs" "--csharp" ""
  emit_row "${tag}_cs" "$(basename "$f")"
done

k=0
for f in "$CORPUS_DIR"/Facades/*.dll; do
  [ -f "$f" ] || continue
  k=$((k+1))
  tag="fac$(printf %03d $k)_$(basename "$f" .dll | cut -c1-16)"
  echo "--- $tag (Facades/$(basename "$f"))"
  run_one "$f" "${tag}_il" "--il" "-il"
  emit_row "${tag}_il" "Facades/$(basename "$f")"
  run_one "$f" "${tag}_cs" "--csharp" ""
  emit_row "${tag}_cs" "Facades/$(basename "$f")"
done

echo
echo "=== summary ==="
column -t -s$'\t' "$OUT/summary.tsv"
echo
echo "verdict tallies:"
awk -F'\t' 'NR>1 {print ($1 ~ /_il$/ ? "il " : "cs "), $2}' "$OUT/summary.tsv" \
  | sort | uniq -c | sort -k2
