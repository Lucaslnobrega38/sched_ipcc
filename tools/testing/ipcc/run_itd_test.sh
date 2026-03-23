#!/bin/bash
# Test ITD classification: does the same workload get the same class
# on P-cores vs E-cores?
#
# Raptor Lake i5-13400U: CPUs 0-3 = P-cores, CPUs 4-11 = E-cores
set -e

DURATION=${1:-10}
PCORE=0
ECORE=4

cd "$(dirname "$0")"

if [ ! -f itd_class_test ]; then
    echo "Compilando..."
    gcc -O2 -mavx2 -pthread -o itd_class_test itd_class_test.c
fi

echo "============================================"
echo "  ITD Classification: P-core vs E-core"
echo "  Duration: ${DURATION}s per test"
echo "============================================"

for WORKLOAD in scalar vector; do
    echo ""
    echo "--- ${WORKLOAD^^} workload ---"

    echo ""
    echo "[P-core (CPU $PCORE)]"
    taskset -c $PCORE ./itd_class_test $WORKLOAD $DURATION

    echo ""
    echo "[E-core (CPU $ECORE)]"
    taskset -c $ECORE ./itd_class_test $WORKLOAD $DURATION
done

echo ""
echo "============================================"
echo "  Test complete."
echo "============================================"
