#! /usr/bin/env bash

TOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
source "${TOP_DIR}/scripts/apollo.bashrc"

if ! "${APOLLO_IN_DOCKER}" ; then
  error "Must be run from within docker container"
  exit 1
fi

while true
do
    bash /apollo/scripts/prediction.sh start
    sleep 10
done

