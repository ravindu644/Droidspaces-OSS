#!/system/bin/sh

# Droidspaces Bug Report Generator
# Collects dmesg, logcat, daemon logs, container logs and AVC denials
# into a single tarball dropped on /sdcard for easy sharing.
#
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

DROIDSPACE_DIR=/data/local/Droidspaces
BUSYBOX="${DROIDSPACE_DIR}/bin/busybox"
LOGS_DIR="${DROIDSPACE_DIR}/Logs"
DATE_TIME="$("${BUSYBOX}" date +"%Y-%m-%d_%H-%M-%S")"
BUGREPORT_DIR="${LOGS_DIR}/bugreport_${DATE_TIME}"
OUTPUT_TARBALL="/sdcard/Droidspaces-bugreport_${DATE_TIME}.tar.gz"

# We need busybox for reliable cross-device tools (tar, sort, grep, etc.)
if [ ! -f "${BUSYBOX}" ] || [ ! -x "${BUSYBOX}" ]; then
    echo "ERROR: busybox not found or not executable at ${BUSYBOX}"
    echo "       Cannot generate bug report without it. Aborting."
    exit 1
fi

# Make sure the logs directory actually exists before we try to work in it
if [ ! -d "${LOGS_DIR}" ]; then
    echo "ERROR: Logs directory not found at ${LOGS_DIR}"
    echo "       Has droidspacesd ever run on this device? Aborting."
    exit 1
fi

echo "Creating bugreport folder..."
mkdir -p "${BUGREPORT_DIR}" 2>/dev/null
if [ ! -d "${BUGREPORT_DIR}" ]; then
    echo "ERROR: Failed to create bugreport directory at ${BUGREPORT_DIR}"
    echo "       Check permissions on ${LOGS_DIR}. Aborting."
    exit 1
fi

# Start with an empty denials file so we can safely append to it later
> "${BUGREPORT_DIR}/avc_denials.txt"

# Grab a fresh dmesg snapshot
echo "Collecting dmesg..."
dmesg > "${BUGREPORT_DIR}/dmesg_${DATE_TIME}.log" 2>&1
if [ $? -ne 0 ]; then
    echo "WARNING: dmesg collection failed, continuing without it"
fi

# Grab current logcat buffer
echo "Collecting logcat..."
logcat -d > "${BUGREPORT_DIR}/logcat_${DATE_TIME}.log" 2>&1
if [ $? -ne 0 ]; then
    echo "WARNING: logcat collection failed, continuing without it"
fi

# Copy the main daemon and boot module logs if they exist
echo "Collecting daemon and boot logs..."
for f in "${LOGS_DIR}/droidspacesd.log" "${LOGS_DIR}/boot-module.log"; do
    if [ -f "${f}" ]; then
        "${BUSYBOX}" cp "${f}" "${BUGREPORT_DIR}/" 2>/dev/null || \
            echo "WARNING: Could not copy $(basename ${f})"
    fi
done

# Copy per-container log folders - the binary drops a log folder per container
# named after the container (e.g. Ubuntu-24.04/, Ubuntu/, etc.)
echo "Collecting container logs..."
for container_log_dir in "${LOGS_DIR}"/*/; do
    # Skip the bugreport folder itself in case of any overlap
    [ "$(realpath "${container_log_dir}")" = "$(realpath "${BUGREPORT_DIR}")" ] && continue
    if [ -d "${container_log_dir}" ]; then
        container_name="$(basename "${container_log_dir}")"
        "${BUSYBOX}" cp -r "${container_log_dir}" "${BUGREPORT_DIR}/${container_name}" 2>/dev/null || \
            echo "WARNING: Could not copy container logs for ${container_name}"
    fi
done

# Strip kernel timestamps ([  12.345]) and logcat prefixes from AVC lines,
# then deduplicate so we don't end up with 200 identical denial lines
generate_denials() {
    cat "$@" \
        | "${BUSYBOX}" grep -i "avc:" \
        | "${BUSYBOX}" sed -E 's/^[[:space:]]*\[[[:space:]]*[0-9:.]+\][[:space:]]*//' \
        | "${BUSYBOX}" sed -E 's/^[0-9-]+ [0-9:.]+[[:space:]]+[0-9]+[[:space:]]+[0-9]+[[:space:]]+[A-Z][[:space:]]*//' \
        | "${BUSYBOX}" sort -u >> "${BUGREPORT_DIR}/avc_denials.txt"
    "${BUSYBOX}" sort -u "${BUGREPORT_DIR}/avc_denials.txt" \
        -o "${BUGREPORT_DIR}/avc_denials.txt"
}

# Pull AVC denials from everything we have - the persistent dmesg.log from
# the KSU log path is especially useful since it covers the full boot
echo "Extracting AVC denials..."
if [ -f "${LOGS_DIR}/dmesg.log" ]; then
    generate_denials "${LOGS_DIR}/dmesg.log" "${BUGREPORT_DIR}"/*.log
    cp "${LOGS_DIR}/dmesg.log" "${BUGREPORT_DIR}/boot_dmesg.log" 2>/dev/null
else
    generate_denials "${BUGREPORT_DIR}"/*.log
fi

# Pack everything into a tarball on /sdcard so it's easy to pull via MTP or adb
echo "Packing bugreport tarball..."
"${BUSYBOX}" tar -czf "${OUTPUT_TARBALL}" \
    -C "${LOGS_DIR}" "bugreport_${DATE_TIME}"
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to create tarball at ${OUTPUT_TARBALL}"
    echo "       Is /sdcard mounted and writable?"
    "${BUSYBOX}" rm -rf "${BUGREPORT_DIR}" 2>/dev/null
    exit 1
fi

# Clean up the temporary folder now that it's packed
"${BUSYBOX}" rm -rf "${BUGREPORT_DIR}" 2>/dev/null

echo "Done! Bug report saved to: ${OUTPUT_TARBALL}"
