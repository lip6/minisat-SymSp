#!/bin/bash

CNF="$1"
shift

$(dirname $0)/CNFBlissSymmetries "$CNF" > "$CNF.txt"
$(dirname $0)/minisat_core "$CNF" "$@"
