#!/bin/bash
# Differential validation harness (the port-baml Phase-11-style hardening run):
# the C++ port's ilspy_cli vs the C# ilspycmd oracle, over the capa-testfiles
# corpus and the net48 reference-assembly corpus, in --il and --csharp modes.
#
# The oracle: the dotnet global tool ilspycmd, version-matched to this tree
# (the C# source is 11.0.0-rc; install with
#   dotnet tool install -g ilspycmd --version 11.0.0.9335-rc
# from a directory WITHOUT the repo's global.json, and export
# DOTNET_ROOT=<the dotnet root> -- see PORT_LOG_BAML.md for the full story
# and the results of the recorded run).
#
# Usage: differential_harness.sh [output-dir]
#   ILSPY_ORACLE   the ilspycmd path (default ~/.dotnet/tools/ilspycmd)
#   ILSPY_PORT     the port's CLI (default: this repo's linux-ninja build)
#   CAPA_DIR       the capa sample dir (default /home/jim/source/capa-testfiles)
#   CORPUS_DIR     the net48 corpus dir (default the fixture install)
#   TIMEOUT        per-run timeout in seconds (default 180)
#
# Every sample runs in both modes on both engines; the outputs are kept under
# <output-dir>/<tag>/{oracle,port}.{txt,err,n.txt,rc} plus diff.txt (the diff
# after CR-stripping -- the port pins CRLF, the oracle uses the host's line
# ending) and a summary.tsv with one row per sample x mode:
#   tag, verdict (IDENTICAL / DIFFERENT / PORT-CRASH(rc) / PORT-FAIL(rc) /
#   ORACLE-FAIL-ONLY / BOTH-FAIL[-IDENTICAL] / TIMEOUT),
#   the two exit codes, the output line counts, and the raw diff-line count.

set -u

REPO=$(cd "$(dirname "$0")/../.." && pwd)
PORT=${ILSPY_PORT:-"$REPO/build/linux-ninja/ILSpyCmd/ilspy_cli"}
ORACLE=${ILSPY_ORACLE:-$HOME/.dotnet/tools/ilspycmd}
export DOTNET_ROOT=${DOTNET_ROOT:-/home/jim/.dotnet}
CAPA_DIR=${CAPA_DIR:-/home/jim/source/capa-testfiles}
CORPUS_DIR=${CORPUS_DIR:-/home/jim/ilspy-test-fixtures/net48}
OUT=${1:-/tmp/diffval/run}
TIMEOUT=${TIMEOUT:-180}

# 15 assemblies across mscorlib/System/WPF/facades.
CORPUS_SAMPLES=(
  mscorlib.dll System.dll System.Core.dll System.Xml.dll System.Xml.Linq.dll
  System.Configuration.dll System.Runtime.Remoting.dll
  WindowsBase.dll System.Xaml.dll PresentationCore.dll PresentationFramework.dll
  Facades/System.Runtime.dll Facades/System.Collections.dll
  Facades/System.Linq.dll Facades/System.Threading.Tasks.dll
)

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

emit_row() {  # $1=tag
  local tag="$1"
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
  elif [ "$rc" = "134" ] || [ "$rc" = "139" ]; then
    verdict="PORT-CRASH($rc)"
  elif [ "$rc" != "0" ]; then
    verdict="PORT-FAIL($rc)"
  elif [ "$(cat "$dir/diff.rc")" = "0" ]; then
    verdict=IDENTICAL
  fi
  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "$tag" "$verdict" "$orc" "$rc" \
    "$olines" "$plines" "$dlines" >> "$OUT/summary.tsv"
  echo "$tag: $verdict (oracle rc=$orc, port rc=$rc, lines o=$olines p=$plines, diff-lines=$dlines)"
}

mkdir -p "$OUT"
printf "tag\tverdict\toracle_rc\tport_rc\toracle_lines\tport_lines\tdiff_lines\n" > "$OUT/summary.tsv"

i=0
for f in "$CAPA_DIR"/*; do
  [ -f "$f" ] || continue
  grep -qa BSJB "$f" 2>/dev/null || continue
  i=$((i+1))
  tag="capa${i}_$(basename "$f" | cut -c1-12)"
  echo "--- $tag ($(basename "$f"))"
  run_one "$f" "${tag}_il" "--il" "-il"
  emit_row "${tag}_il"
  run_one "$f" "${tag}_cs" "--csharp" ""
  emit_row "${tag}_cs"
done

j=0
for a in "${CORPUS_SAMPLES[@]}"; do
  f="$CORPUS_DIR/$a"
  [ -f "$f" ] || { echo "MISSING corpus sample $a"; continue; }
  j=$((j+1))
  tag="corp${j}_$(basename "$a" .dll)"
  echo "--- $tag ($a)"
  run_one "$f" "${tag}_il" "--il" "-il"
  emit_row "${tag}_il"
  run_one "$f" "${tag}_cs" "--csharp" ""
  emit_row "${tag}_cs"
done

echo
echo "=== summary ==="
column -t -s$'\t' "$OUT/summary.tsv"
echo
echo "verdict tallies:"
awk -F'\t' 'NR>1 {print ($1 ~ /_il$/ ? "il " : "cs "), $2}' "$OUT/summary.tsv" \
  | sort | uniq -c | sort -k2
