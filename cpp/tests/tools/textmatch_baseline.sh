#!/bin/bash
# The Phase-11 broad text-match baseline (PORT_PLAN.md Phase 11, exit
# criterion 1): the C#-emission pipeline of the port vs the ilspycmd
# oracle, over a broad method-set sample, whole-file --csharp runs.
#
# The sample set:
#   * the top-N corpus assemblies ranked by public-surface size -- the
#     metric is the ORACLE's own --csharp emission line count (the size
#     of the decompilable surface the exit criterion compares), over
#     every .dll in CORPUS_DIR;
#   * the capa-testfiles .NET set (every BSJB-bearing file; the native
#     PEs both engines reject are part of the record);
#   * one extra body-bearing real assembly (EXTRA_SAMPLE -- the oracle
#     tool's own ilspycmd.dll) so the matrix exercises actual method
#     bodies, not just the metadata-only reference shape.
#
# Per sample: both engines, CR-normalized diff, and a category:
#   IDENTICAL / CONVENTION-DIFF (the port output matches the documented
#   Phase-5 seed shape: no usings/namespaces/assembly attributes) /
#   REAL-MISMATCH / PORT-CRASH(rc) / PORT-FAIL(rc) / ORACLE-THROWS /
#   TIMEOUT. The results land in <out>/summary.tsv.
#
# Usage: textmatch_baseline.sh [out-dir]
#   ILSPY_ORACLE / ILSPY_PORT / CORPUS_DIR / CAPA_DIR / TIMEOUT / TOP_N
#   (the same conventions as differential_harness.sh).
#   MODE=il switches the run to whole-module --il on both engines (the
#   default MODE=csharp runs --csharp); ALL_CORPUS=1 ranks-and-runs every
#   corpus .dll instead of the top-$TOP_N (the full-corpus parity sweep).

set -u

REPO=$(cd "$(dirname "$0")/../.." && pwd)
PORT=${ILSPY_PORT:-"$REPO/build/linux-ninja/ILSpyCmd/ilspy_cli"}
ORACLE=${ILSPY_ORACLE:-$HOME/.dotnet/tools/ilspycmd}
export DOTNET_ROOT=${DOTNET_ROOT:-/home/jim/.dotnet}
CORPUS_DIR=${CORPUS_DIR:-/home/jim/ilspy-test-fixtures/net48}
CAPA_DIR=${CAPA_DIR:-/home/jim/source/capa-testfiles}
EXTRA_SAMPLE=${EXTRA_SAMPLE:-$(ls /home/jim/.dotnet/tools/.store/ilspycmd/*/ilspycmd/*/tools/net10.0/any/ilspycmd.dll 2>/dev/null | head -1)}
TOP_N=${TOP_N:-20}
MODE=${MODE:-csharp}
case "$MODE" in
  csharp) PORT_FLAGS="--csharp"; ORACLE_FLAGS="" ;;
  il)     PORT_FLAGS="--il"; ORACLE_FLAGS="-il" ;;
  *) echo "MODE must be csharp or il" >&2; exit 2 ;;
esac
ALL_CORPUS=${ALL_CORPUS:-0}
OUT=${1:-/tmp/textmatch}
TIMEOUT=${TIMEOUT:-180}

mkdir -p "$OUT"
printf "tag\tcategory\toracle_rc\tport_rc\toracle_lines\tport_lines\tdiff_lines\tnote\n" \
  > "$OUT/summary.tsv"

run_one() {  # $1=sample $2=tag
    local sample="$1" tag="$2" dir="$OUT/$tag"
    mkdir -p "$dir"
    timeout "$TIMEOUT" "$ORACLE" $ORACLE_FLAGS "$sample" > "$dir/oracle.txt" 2> "$dir/oracle.err"
    echo $? > "$dir/oracle.rc"
    timeout "$TIMEOUT" "$PORT" $PORT_FLAGS "$sample" > "$dir/port.txt" 2> "$dir/port.err"
    echo $? > "$dir/port.rc"
    tr -d '\r' < "$dir/oracle.txt" > "$dir/oracle.n.txt"
    tr -d '\r' < "$dir/port.txt" > "$dir/port.n.txt"
    diff "$dir/oracle.n.txt" "$dir/port.n.txt" > "$dir/diff.txt"
    echo $? > "$dir/diff.rc"
}

emit_row() {  # $1=tag
    local tag="$1" dir="$OUT/$tag"
    local orc rc verdict note="" olines plines dlines=0
    orc=$(cat "$dir/oracle.rc"); rc=$(cat "$dir/port.rc")
    olines=$(wc -l < "$dir/oracle.n.txt")
    plines=$(wc -l < "$dir/port.n.txt")
    if [ "$(cat "$dir/diff.rc")" != "0" ]; then
        dlines=$(grep -cE "^[<>]" "$dir/diff.txt")
    fi
    if [ "$orc" = "124" ] || [ "$rc" = "124" ]; then
        verdict=TIMEOUT
    elif [ "$orc" != "0" ]; then
        verdict=ORACLE-THROWS
        note=$(head -1 "$dir/oracle.err" | cut -c1-90)
    elif [ "$rc" = "134" ] || [ "$rc" = "139" ]; then
        verdict="PORT-CRASH($rc)"
        note=$(head -1 "$dir/port.err" | cut -c1-90)
    elif [ "$rc" != "0" ]; then
        verdict="PORT-FAIL($rc)"
        note=$(head -1 "$dir/port.err" | cut -c1-90)
    elif [ "$(cat "$dir/diff.rc")" = "0" ]; then
        verdict=IDENTICAL
    else
        # The port produced text that differs: the Phase-5 seed shape (no
        # usings / namespaces / assembly attributes) is the documented
        # convention surface; anything attempting the full-file shape is
        # a real mismatch.
        if grep -qE "^(using |namespace |\[assembly:)" "$dir/port.n.txt"; then
            verdict=REAL-MISMATCH
        else
            verdict=CONVENTION-DIFF
        fi
    fi
    printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "$tag" "$verdict" "$orc" "$rc" \
        "$olines" "$plines" "$dlines" "$note" >> "$OUT/summary.tsv"
    echo "$tag: $verdict (oracle $orc/$olines lines, port $rc/$plines lines, diff $dlines)"
}

# --- 1. rank the corpus by the oracle's C# emission size.
echo "ranking $CORPUS_DIR by the oracle's --csharp emission..."
: > "$OUT/ranking.txt"
for f in "$CORPUS_DIR"/*.dll; do
    n=$(timeout "$TIMEOUT" "$ORACLE" "$f" 2>/dev/null | wc -l)
    echo "$n $(basename "$f")" >> "$OUT/ranking.txt"
done
if [ "$ALL_CORPUS" = "1" ]; then
  sort -rn "$OUT/ranking.txt" | awk '{print $2}' > "$OUT/top.txt"
else
  sort -rn "$OUT/ranking.txt" | head -"$TOP_N" | awk '{print $2}' \
    > "$OUT/top.txt"
fi

# --- 2. the sample list.
: > "$OUT/samples.txt"
while IFS= read -r name; do
    echo "corp|$name|$CORPUS_DIR/$name" >> "$OUT/samples.txt"
done < "$OUT/top.txt"
i=0
for f in "$CAPA_DIR"/*; do
    [ -f "$f" ] || continue
    grep -qa BSJB "$f" 2>/dev/null || continue
    i=$((i+1))
    echo "capa|capa${i}_$(basename "$f" | cut -c1-12)|$f" >> "$OUT/samples.txt"
done
if [ -n "$EXTRA_SAMPLE" ] && [ -f "$EXTRA_SAMPLE" ]; then
    echo "extra|ilspycmd_self|$EXTRA_SAMPLE" >> "$OUT/samples.txt"
fi

# --- 3. the runs + the classification.
while IFS='|' read -r kind tag sample; do
    echo "--- $tag"
    run_one "$sample" "$tag"
    emit_row "$tag"
done < "$OUT/samples.txt"

echo
echo "=== the matrix ==="
awk -F'\t' 'NR>1 {print $2}' "$OUT/summary.tsv" | sort | uniq -c | sort -rn
