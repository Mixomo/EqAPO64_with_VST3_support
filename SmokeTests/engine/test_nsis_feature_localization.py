"""Native NSIS language expansion and selection stability, without system installation."""
import argparse,json,subprocess
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args();root=Path(__file__).resolve().parents[2]
    case=args.output.resolve();case.mkdir(parents=True,exist_ok=True)
    keys=['FeatureBundledIR','FeatureHeadphoneCal','FeatureRackActions','FeatureRestorePoint','DescBundledIR','DescHeadphoneCal','DescRackActions','DescRestorePoint','InvalidFeatureArguments']
    source=['Unicode true','SilentInstall silent','RequestExecutionLevel user','!include "MUI2.nsh"','!include "Sections.nsh"','!include "FileFunc.nsh"',f'OutFile "{case / "test.exe"}"',
            '!insertmacro MUI_LANGUAGE "English"','!insertmacro MUI_LANGUAGE "Spanish"','!insertmacro MUI_LANGUAGE "German"',f'!include "{root / "Setup/FeatureStrings.nsh"}"']
    for index,key in enumerate(keys[:4]):source+=['Section "$('+key+')" Sec'+str(index),'SectionEnd']
    source+=['Function .onInit',' ${GetParameters} $0',' ${GetOptions} $0 "/LANG=" $LANGUAGE','FunctionEnd']
    source+=['Section "-Probe"',f' FileOpen $9 "{case}\\runtime-$LANGUAGE.txt" w']
    for index in range(4):source+=[' !insertmacro '+('SelectSection' if index%2 else 'UnselectSection')+' ${Sec'+str(index)+'}']
    for key in keys:source+=[' FileWrite $9 "$LANGUAGE|'+key+'|$('+key+')$\\r$\\n"']
    for index in range(4):source+=[' SectionGetFlags ${Sec'+str(index)+'} $0',' FileWrite $9 "$LANGUAGE|flags'+str(index)+'|$0$\\r$\\n"']
    source+=[' FileClose $9','SectionEnd'];script=case/'test.nsi';script.write_text('\n'.join(source))
    build=subprocess.run([str(root/'third_party/nsis-3.11/makensis.exe'),'/NOCD',str(script)],capture_output=True,text=True,timeout=60)
    (case/'compile.log').write_text(build.stdout+build.stderr)
    if build.returncode:raise RuntimeError('NSIS_COMPILE_FAILED')
    rows={};effective_languages={}
    for requested in (1033,1034,1031,1036):
        subprocess.run([str(case/'test.exe'),'/S','/LANG='+str(requested)],check=True,timeout=10)
        file=max(case.glob('runtime-*.txt'),key=lambda path:path.stat().st_mtime_ns)
        rows[str(requested)]={}
        for line in file.read_text(encoding='utf-8-sig').splitlines():
            language,key,value=line.split('|',2);rows[str(requested)][key]=value;effective_languages[str(requested)]=language
    errors=[]
    for language in ('1033','1034','1031','1036'):
        for index in range(4):
            if int(rows[language]['flags'+str(index)])&1!=index%2:errors.append(language+': selection changed')
        for key in keys:
            if not rows[language][key] or '$(' in rows[language][key]:errors.append(language+': missing '+key)
            if language in ('1034','1031') and rows[language][key]==rows['1033'][key]:errors.append(language+': untranslated '+key)
            if language=='1036' and rows[language][key]!=rows['1033'][key]:errors.append('English fallback failed: '+key)
    result={'status':'FAIL' if errors else 'PASS','runtime_languages':rows,'effective_languages':effective_languages,'errors':errors,'limits':'Actual NSIS text/flags and unsupported-language fallback; native page rendering and persisted registry selections are not covered.'}
    (case/'result.json').write_text(json.dumps(result,indent=2));print(result['status'],errors)
    if errors:raise SystemExit(1)

if __name__=='__main__':main()
