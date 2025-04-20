#!/bin/bash


set -e
set -o pipefail


COUNTER_TMP_FILE="/tmp/.rerun_test_command_tmp_file"


sleep 1


if [[ ! -e "${COUNTER_TMP_FILE}" ]]; then
	counter=1
else
	counter=$(cat "${COUNTER_TMP_FILE}")
fi


if [[ "${counter}" == "10" ]]; then
	rm -f "${COUNTER_TMP_FILE}"
	echo "test_command.sh: exit 1"
	exit 1
else
	echo "test_command.sh: exit 0"
	((counter++))
	echo "${counter}" > "${COUNTER_TMP_FILE}"
fi
