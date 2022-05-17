#! /usr/bin/env bash
set -e

TOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source "${TOP_DIR}/scripts/apollo.bashrc"
scenoRITA_repo="https://github.com/ivanium/autoT"

if ! "${APOLLO_IN_DOCKER}" ; then
  error "Must be run from within docker container"
  exit 1
fi

pip3 install deap networkx pandas sklearn kneed

git clone $scenoRITA_repo automation

success "Finished preparing container for scenoRITA"
