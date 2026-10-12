"""Populate qmake's compiler probe cache from actual CL preprocessor output.

Some restricted Windows environments drop QProcess system() probe stdout.
This performs the same probe through the build shell, without guessing versions.
"""
from pathlib import Path
import re,sys
probe=Path(sys.argv[1]).read_text(errors='replace')
values=dict(re.findall(r'^(QT_COMPILER_STDCXX|QMAKE_MSC_VER|QMAKE_MSC_FULL_VER)\s*=\s*(\S+)',probe,re.M))
if len(values)!=3: raise SystemExit('CL compiler macro probe incomplete')
lines=['QMAKE_CXX.COMPILER_MACROS = '+' '.join(values)]
lines += ['QMAKE_CXX.'+key+' = '+value for key,value in values.items()]
Path(sys.argv[2],'.qmake.stash').write_text('\n'.join(lines)+'\n')
