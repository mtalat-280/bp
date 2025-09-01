#!/usr/bin/env python3
# Usage: ./add_nce_allowlist.py <file>
# Example: ./add_nce_allowlist.py west.yml

import sys
from ruamel.yaml import YAML

filename = sys.argv[1] if len(sys.argv) > 1 else "west.yml"

yaml = YAML()
yaml.preserve_quotes = True

with open(filename) as f:
    data = yaml.load(f)

# Navigate to the name-allowlist under zephyr -> import
projects = data['manifest']['projects']
for proj in projects:
    if proj.get('name') == 'zephyr':
        allowlist = proj.get('import', {}).get('name-allowlist', [])
        if 'nce-sdk' not in allowlist:
            allowlist.append('nce-sdk')
            allowlist.sort()
        proj['import']['name-allowlist'] = allowlist
        break

with open(filename, 'w') as f:
    yaml.dump(data, f)

print(f"Added nce-sdk to {filename} alphabetically.")
