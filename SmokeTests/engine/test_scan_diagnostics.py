"""Check isolated scanner diagnostics without changing the installed configuration."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--plugin', type=Path, required=True)
    args = parser.parse_args()
    host, plugin = args.host.resolve(), args.plugin.resolve()
    if not host.is_file() or not plugin.exists():
        parser.error('Provide an existing host and a valid plugin path')
    with tempfile.TemporaryDirectory(prefix='EqApoScan-') as directory:
        root = Path(directory)
        missing = root / 'missing-\u00f1.vst3'
        invalid = root / 'invalid.vst3'
        invalid.write_bytes(b'This file is not a Windows plugin binary.')
        for name, path, expected_error in (
            ('valid', plugin, None), ('missing', missing, 2), ('invalid', invalid, 193)
        ):
            report = root / (name + '.json')
            result = subprocess.run([str(host), '--scan', '--plugin', str(path),
                                     '--result', str(report)], capture_output=True, timeout=10)
            data = json.loads(report.read_text(encoding='utf-8'))
            if expected_error is None:
                assert result.returncode == 0 and data['status'] == 'PASS', data
            else:
                assert result.returncode == 1 and data['status'] == 'FAIL', data
                assert data['win32_error'] == expected_error, data
                assert data['path'] == str(path) and data['load_path'] == str(path), data
                assert data['message'].strip(), data
            print(name + ': PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
