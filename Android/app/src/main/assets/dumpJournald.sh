#!/bin/sh

journalctl -u "$1" --no-pager -n "$2" 2>/dev/null