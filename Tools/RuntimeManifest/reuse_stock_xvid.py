"""Select the measured stock ARM64 movie DLL before provenance and native codec testing."""
import argparse
import hashlib
import io
from pathlib import Path
import subprocess
from urllib.request import urlopen
import zipfile

# Exact codec sources, headers, pinned upstream source recipe and CMake wiring
# of build 41061576677.1 (client 471ee42f). A codec/build input change uses
# the fresh local build and still runs the same native movie test.
INPUTS = {
    'CMakeLists.txt': '95dee8a89c6f2c2c000a0251dc475b2f50ad1fc1',
    'ThirdParty/CMakeLists.txt': '611c24e5923db184858b09f0460909f92aff8a3b',
    'cmake': '9047066adc053cbdb4fdac2aaee60477a20558d0',
    'Tools/XvidDecoder': 'd4ec48beb517ff634178ecf5e9c9e1f489737f3a',
    'ThirdParty/xvidcore': 'b289f4e776860b62cde83083dae068848c21890a',
    'ThirdParty/DivxDecoder': '5923ac3fc81cd136a965905b552cbb8eaeb03f60',
}
ARCHIVE = 'https://github.com/Russianranger/esteria_android/releases/download/deps-client-arm64-33cf433-471ee42f/reconstructed-client-arm64-original.zip'
ARCHIVE_SIZE = 45848847
ARCHIVE_SHA256 = 'befac98c5c3434ea94dcb878f5da37f915946221fb17f1a7888c65f74bb846ad'
FILES = {
    'XvidDecoder.dll': 'b4c774430942d709ca7e3f03a1890d6aec7aaa2e432bee0e698e57027a70d006',
    'XvidDecoder.pdb': '475b4fe1cf2551857370c680dfb5e1faacc9ccfa43f4dcd597983c319f7b3334',
}


def select(directory, opener=urlopen):
    for path, pin in INPUTS.items():
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD:' + path], text=True).strip()
        if actual != pin:
            print('Codec inputs changed; exercising the freshly built XvidDecoder.')
            return
    with opener(ARCHIVE, timeout=120) as response:
        raw = response.read(ARCHIVE_SIZE + 1)
    if len(raw) != ARCHIVE_SIZE or hashlib.sha256(raw).hexdigest() != ARCHIVE_SHA256:
        raise ValueError('stock ARM64 client archive identity changed')
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        selected = {name: archive.read('bin/Win/ARM64/' + name) for name in FILES}
    if any(hashlib.sha256(data).hexdigest() != FILES[name] for name, data in selected.items()):
        raise ValueError('stock codec or symbols differ from original measurements')
    for name, data in selected.items():
        (directory / name).write_bytes(data)
    print('Selected the exact 471ee42f movie DLL and matching symbols for native qualification.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    select(parser.parse_args().directory)
