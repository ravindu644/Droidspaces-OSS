#!/bin/sh
case "$1" in
--running)
systemctl list-units --type=service --state=running --no-legend --no-pager 2>/dev/null | while read -r line; do
name=$(echo "$line" | awk '{print $1}')
desc=$(echo "$line" | awk '{$1=$2=$3=$4=""; print $0}' | sed 's/^[[:space:]]*//')
echo "${name}|${desc}"
done
;;
--all)
systemctl list-unit-files --type=service --no-legend --no-pager 2>/dev/null | while read -r line; do
name=$(echo "$line" | awk '{print $1}')
state=$(echo "$line" | awk '{print $2}')
echo "${name}|${state}"
done
;;
esac
