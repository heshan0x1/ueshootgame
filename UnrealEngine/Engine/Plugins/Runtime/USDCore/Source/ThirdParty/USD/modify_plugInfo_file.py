import pathlib
import re
import sys


def replaceLibraryPath(jsonFilePath, binariesPath):
    '''
    Searches through the plugInfo.json file at the path given by jsonFilePath
    for a "LibraryPath" field and replaces its value with a path that has the
    directory portion of the path replaced by binariesPath.
    '''
    if not isinstance(jsonFilePath, pathlib.Path):
        jsonFilePath = pathlib.Path(jsonFilePath)
    if not isinstance(binariesPath, pathlib.Path):
        binariesPath = pathlib.Path(binariesPath)

    modified = False
    outputLines = []

    pattern = re.compile('"LibraryPath": "([^"]+)"')

    with jsonFilePath.open(mode='r+') as jsonFile:
        for jsonLine in jsonFile.readlines():
            match = pattern.search(jsonLine)
            if not match:
                outputLines.append(jsonLine)
                continue

            matchedString = match.group(1)

            if "@PLUG_INFO_LIBRARY_PATH@" in matchedString:
                # Ignore code templates
                outputLines.append(jsonLine)
                continue

            libPath = pathlib.Path(matchedString)
            newLibPath = binariesPath / libPath.name
            newJsonLine = re.sub(
                matchedString, newLibPath.as_posix(), jsonLine)
            outputLines.append(newJsonLine)
            modified = True

        if modified:
            jsonFile.seek(0)
            jsonFile.writelines(outputLines)
            jsonFile.truncate()

    return modified


def main():
    import argparse
    import os

    programName = os.path.basename(sys.argv[0])
    parser = argparse.ArgumentParser(
        prog=programName,
        description='Replaces "LibraryPath" in a plugInfo.json file')

    parser.add_argument(
        'jsonFilePath', action='store', type=pathlib.Path,
        help='plugInfo.json file path')
    parser.add_argument(
        'binariesPath', action='store', type=pathlib.Path,
        help='binaries directory path to replace in "LibraryPath"')

    args = parser.parse_args()

    replaceLibraryPath(args.jsonFilePath, args.binariesPath)

    return 0


if __name__ == '__main__':
    sys.exit(main())
