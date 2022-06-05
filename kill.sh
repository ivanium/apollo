#! /usr/bin/env bash
set -e

TOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source "${TOP_DIR}/scripts/apollo.bashrc"

if ! "${APOLLO_IN_DOCKER}" ; then
  error "Must be run from within docker container"
  exit 1
fi

bash /apollo/automation/auxiliary/modules/stop_modules.sh
pkill mainboard
