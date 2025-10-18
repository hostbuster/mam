#!/usr/bin/env bash
set -euo pipefail

exe=${1:-./build/mam}
json=${2:-examples/rack/acid303_sidechain_spectral_4bars.json}
dur=${3:-2}
sr=${4:-48000}
seed=${5:-42}

run() {
  # prints only the SHA1 hash
  mode=$1; shift
  wav=$1; shift
  sha_line=$("$exe" --rack "$json" --wav "$wav" --sr "$sr" --duration "$dur" "$@" --sha1 --random-seed "$seed" | sed -n 's/^SHA1(samples): //p' | tail -n1)
  echo "$sha_line"
}

sha_ref=$(run baseline base.wav --offline-scheduler baseline --offline-block 256)
sha_base=$(run topo-serial topo_serial.wav --topo-scheduler topo --topo-threads 0 --topo-offline-blocks 256)
sha_par=$(run topo-parallel topo_parallel.wav --topo-scheduler topo --topo-threads 4 --topo-offline-blocks 256)

echo "REF: $sha_ref"
echo "SER: $sha_base"
echo "PAR: $sha_par"

if [[ "$sha_ref" == "$sha_base" && "$sha_ref" == "$sha_par" ]]; then
  echo "OK: parity confirmed"
else
  echo "ERROR: parity mismatch"; exit 1
fi
