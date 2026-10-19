#!/usr/bin/env python3
"""Read-only iOS toolchain preflight; does not build, sign or inspect credentials."""
import json, platform, shutil, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
tools={name:shutil.which(name) for name in ('xcodebuild','xcrun','xcodegen')}
sdk=None
if tools['xcrun']:
    run=subprocess.run([tools['xcrun'],'--sdk','iphoneos','--show-sdk-path'],capture_output=True,text=True)
    if run.returncode==0:sdk=run.stdout.strip()
framework=root/'build/frameworks/MaxPS4FEXRuntime.framework'
report={'host':platform.system(),'architecture':platform.machine(),'tools':tools,
        'iphoneos_sdk':sdk,'project_spec':(root/'MaxPS4-iOS/project.yml').is_file(),
        'fex_framework_present':framework.is_dir(),
        'build_prerequisites_present':all(tools.values()) and bool(sdk) and framework.is_dir(),
        'signing_checked':False,'device_tested':False,'ipa_built':False,
        'remaining':['Apple macOS/Xcode with iPhoneOS SDK and xcodegen',
                     'arm64 iOS FEX runtime built and linked against the pinned dependencies',
                     'authorized development signing/provisioning and JIT execution method',
                     'physical iPhone run: translated CPU fault/stack/TLS checks and Metal validation']}
print(json.dumps(report,indent=2))
