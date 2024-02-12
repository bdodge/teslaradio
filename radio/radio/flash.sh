#!/bin/sh

SCRIPT="$0"
PRODUCT="$(echo "$1" | tr '[:upper:]' '[:lower:]')" # force lowercase
REVISION="$(echo "$2" | tr '[:upper:]' '[:lower:]')" # force lowercase
if [ "$PRODUCT" = "" ] || [ "$REVISION" = "" ]; then
	echo "usage: $SCRIPT product revision\ne.g. $SCRIPT inner evt"
	exit 1
fi
product_name=""
product_short=""
case $PRODUCT in
"teslaradio")
	product_name="teslaradio"
	;;
*)
	echo "unknown product $PRODUCT, should be teslaradio"
	exit 1
esac
revision_name=""
case $REVISION in
"nrf5340dk_nrf5340_cpuapp"|"5340dk"|"5340")
	revision_name="nrf5340dk_nrf5340_cpuapp"
	;;
*)
	echo "unknown revision, should be 5340dk"
	exit 1
esac

BOARD_NAME=${revision_name}
BOARD_ROOT=${PWD}/..

echo "flashing "build_${BOARD_NAME}/zephyr/merged.hex

nrfjprog -f nrf53  --coprocessor CP_APPLICATION --program build_${BOARD_NAME}/zephyr/merged.hex --sectorerase --verify --reset

