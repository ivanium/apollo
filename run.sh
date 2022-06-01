#! /usr/bin/env bash
set -e

TOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source "${TOP_DIR}/scripts/apollo.bashrc"

if ! "${APOLLO_IN_DOCKER}" ; then
  error "Must be run from within docker container"
  exit 1
fi

bash /apollo/automation/auxiliary/modules/stop_modules.sh
sleep 3
bash /apollo/scripts/bootstrap.sh restart
sleep 3
bash /apollo/automation/auxiliary/modules/start_modules.sh
sleep 3

echo "Environment setup finished. Starting scenoRITA"
pushd /apollo/automation/scenario_generator/
bazel run scenoRITA_mut
popd


