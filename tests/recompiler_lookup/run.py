#!/usr/bin/env python3
"""Build synthetic generated lookup comparisons; never load or execute a game."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def call(args, expected=0):
    result = subprocess.run([str(a) for a in args], check=False, timeout=1800)
    if result.returncode != expected:
        raise RuntimeError(f'exit {result.returncode}, expected {expected}: {args}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--work-dir', type=Path, required=True, help='New local directory; retain logs and immutable snapshots')
    parser.add_argument('--baseline-header', type=Path, help='Explicit pre-change emitter header; otherwise read local baseline Git ref')
    parser.add_argument('--baseline-ref', default='84568fcadc')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--generator')
    parser.add_argument('--cc')
    parser.add_argument('--cxx')
    parser.add_argument('--benchmark', action='store_true', help='Run short ABBA timings after correctness; use an otherwise idle CPU')
    args = parser.parse_args()
    source, work = args.source.resolve(), args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=False)
    if args.baseline_header:
        baseline = args.baseline_header.read_bytes()
        baseline_source = str(args.baseline_header.resolve())
    else:
        baseline = subprocess.check_output(['git', '-c', f'safe.directory={source}', '-C', str(source),
            'show', args.baseline_ref + ':src/core/recompiler/arm64_to_c.h'])
        baseline_source = args.baseline_ref
    candidate_path = source / 'src/core/recompiler/arm64_to_c.h'
    candidate = candidate_path.read_bytes()
    for tag, data in (('baseline', baseline), ('candidate', candidate)):
        header = work / tag / 'core/recompiler/arm64_to_c.h'
        header.parent.mkdir(parents=True)
        header.write_bytes(data)
    metadata = dict(baseline_source=baseline_source, baseline_header_sha256=hashlib.sha256(baseline).hexdigest(),
                    candidate_header_sha256=hashlib.sha256(candidate).hexdigest(), input='synthetic metadata only',
                    flags='MSVC /O2 /Ob2 /bigobj /fp:precise /MD, no LTO; other compilers -O2')
    (work / 'metadata.json').write_text(json.dumps(metadata, indent=2), encoding='utf8')
    build = work / 'build'
    options = [f'-DLOOKUP_BASELINE_INCLUDE={work / "baseline"}', f'-DLOOKUP_CANDIDATE_INCLUDE={work / "candidate"}',
               '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release']
    if args.generator:
        options += ['-G', args.generator]
    for key, value in (('C', args.cc), ('CXX', args.cxx)):
        if value:
            options.append(f'-DCMAKE_{key}_COMPILER={value}')
    call([args.cmake, '-S', Path(__file__).resolve().parent, '-B', build, *options])
    call([args.cmake, '--build', build, '--config', 'Release', '--parallel', '2'])
    executable = next((p for p in (build / 'lookup_test', build / 'lookup_test.exe', build / 'Release/lookup_test.exe') if p.is_file()), None)
    if not executable:
        raise RuntimeError('lookup_test was not built')
    call([executable])
    call([executable, '--inject-mismatch'], expected=1)
    if args.benchmark:
        call([executable, '--bench-only'])
    print(json.dumps(dict(executable=str(executable), work_dir=str(work), benchmark_run=args.benchmark)), flush=True)


if __name__ == '__main__':
    main()
