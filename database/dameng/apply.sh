#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    printf 'Usage: %s <generated-sql-directory>\n' "$0" >&2
    exit 2
fi

: "${DM_HOME:?set DM_HOME to the DM8 installation directory}"
: "${DM_USER:?set DM_USER to the new schema owner}"
: "${DM_PASSWORD:?set DM_PASSWORD for that user}"
DM_ADDRESS=${DM_ADDRESS:-localhost:5236}

sql_dir=$(cd "$1" && pwd)
disql="$DM_HOME/bin/disql"
LD_LIBRARY_PATH="$DM_HOME/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH

for phase in preflight ddl seed fks triggers verify; do
    printf 'Applying %s\n' "$phase"
    "$disql" -L "$DM_USER/\"$DM_PASSWORD\"@$DM_ADDRESS" "@$sql_dir/$phase.sql"
done
