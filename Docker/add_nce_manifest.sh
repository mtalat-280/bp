#!/bin/bash
# Usage: ./add_nce_manifest.sh <revision>
# Example: ./add_nce_manifest.sh main

REVISION=${1:-main}        # Default to "main" if no argument given

# Ensure the target directory exists
mkdir -p /workdir/nce/zephyr/submanifests

# Write the manifest
cat <<EOF > /workdir/nce/zephyr/submanifests/nce.yaml
manifest:
  projects:
    - name: nce-sdk
      url: https://github.com/1NCE-GmbH/1nce-iot-c-sdk
      revision: $REVISION
EOF

echo "Created nce.yaml with revision: $REVISION"

# Navigate to NCS directory
NCS_DIR=/workdir/nce
if [ ! -d "$NCS_DIR" ]; then
    echo "Directory $NCS_DIR not found"
    exit 1
fi

cd "$NCS_DIR" || exit 1

# Run west update
west update -o=--depth=1 -n

