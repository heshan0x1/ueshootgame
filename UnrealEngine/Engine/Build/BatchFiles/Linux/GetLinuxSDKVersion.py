import argparse
import json
import os

SCRIPT_DIR = os.path.dirname(__file__)
DEFAULT_LINUX_SDK_JSON = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..', '..', 'Config', 'Linux', 'Linux_SDK.json'))

if __name__ == '__main__':
	parser = argparse.ArgumentParser()
	parser.add_argument('path_to_json_file', nargs='?', default=DEFAULT_LINUX_SDK_JSON)
	args = parser.parse_args()

	with open(args.path_to_json_file) as f:
		sdk_json = json.load(f)

	print(sdk_json['MainVersion'])