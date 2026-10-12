"""Explicit neutral external-plugin cases. No implicit installed-plugin discovery."""
import json
from pathlib import Path

def load_cases(path):
    if path is None:
        return []
    cases = json.loads(Path(path).read_text(encoding='utf-8-sig'))['cases']
    if not cases:
        raise ValueError('At least one case is required')
    for case in cases:
        plugin = case.get('path', '')
        if not plugin or '<' in plugin or '>' in plugin or not Path(plugin).is_file():
            raise ValueError('Replace path placeholders with the existing plugin binary: '+plugin)
        if not case.get('name') or Path(case['name']).name != case['name'] or case['name'] in ('.', '..'):
            raise ValueError('Case name must be a single directory name')
    return cases
