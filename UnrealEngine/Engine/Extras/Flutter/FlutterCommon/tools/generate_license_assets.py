# Copyright Epic Games, Inc. All Rights Reserved.

import argparse
import hashlib
import json
import os
import re
import stat
import sys

parser = argparse.ArgumentParser(
    description='Takes a directory containing license files, and produces a manifest with associated assets that can be loaded at runtime in Flutter.',
    formatter_class=argparse.ArgumentDefaultsHelpFormatter,
)
parser.add_argument('-i', '--input', default='licenses', type=str, metavar='INPUT_PATH', help='The path containing directories with license files')
parser.add_argument('-o', '--output', default='assets/licenses', type=str, metavar='OUTPUT_PATH', help='The path to which to write the manifest and text assets')

args = parser.parse_args(sys.argv[1:])

notices_directory = args.input
output_directory = args.output
text_directory = os.path.join(output_directory, 'text')
manifest_path = os.path.join(output_directory, 'manifest.json')

if not os.path.exists(notices_directory):
    print(f'Invalid input path "{notices_directory}"')
    sys.exit(1)

# Check that we're not about to overwrite something unrelated
if os.path.exists(text_directory) and not os.path.exists(manifest_path):
    response = input(
        f'Output path "{output_directory}" exists, but does not contain a manifest file.\n'
        f'All files in the the path "{text_directory}" will be deleted and re-generated.\n'
        'Are you sure you want to continue? (Y/N) ')
    
    if response.lower() != 'y':
        print('Aborting')
        sys.exit(1)

os.makedirs(output_directory, exist_ok=True)
os.makedirs(text_directory, exist_ok=True)

license_pattern = re.compile(
    r'=*\n'
    r'Software Name: ([^\n]*)\n'
    r'Version: ([^\n]*)\n'
    r'URL: .*\n'
    r'===+\n'
    r'(.*)',

    flags=re.DOTALL
)

# Clear existing license text in the output directory since we intend to replace them all
for license_name in os.listdir(text_directory):
    license_path = os.path.join(text_directory, license_name)
    os.chmod(license_path, stat.S_IWRITE) # Clear read-only flag so we can delete files tracked by Perforce
    os.remove(license_path)

manifest = {}
seen_hashes = set()

for input_directory_item in sorted(os.listdir(notices_directory), key=str.lower):
    # Find the license file
    input_directory_path = os.path.join(notices_directory, input_directory_item)

    # Skip any paths that don't point to a directory
    if os.path.isdir(input_directory_path) is False:
        continue 

    license_path = None

    for file_name in os.listdir(input_directory_path):
        if file_name.lower().endswith('.license'):
            license_path = os.path.join(input_directory_path, file_name)
            break
    
    if not license_path:
        print(f'No license found for directory {input_directory_path}')
        sys.exit(1)
    
    # Parse the license file
    with open(license_path, 'r', encoding='utf-8') as license_file:
        match = license_pattern.search(license_file.read())
        if not match:
            print(f'Invalid license format for file {license_path}')
            sys.exit(1)
        
        name, version, license_text = match.groups()

    # Make a hash for each license so we only store one copy of each
    license_hash = hashlib.md5(license_text.encode()).hexdigest()

    if license_hash not in seen_hashes:
        with open(os.path.join(text_directory, license_hash), 'w', encoding='utf-8') as license_text_file:
            license_text_file.write(license_text)

    manifest[name] = {
        'version': version,
        'file': license_hash
    }

# Write manifest
with open(manifest_path, 'w') as manifest_file:
    manifest_file.write(json.dumps(manifest))

print(f'Asset generation complete. If this is a Perforce repo, you should reconcile the "{output_directory}" directory.')