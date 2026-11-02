"""Line-coverage report for the translation-profiles work, from an OpenCppCoverage Cobertura export.

Reports three numbers, kept separate on purpose:
  * changed core lines: lines of non-UI, non-self-test windows/src files that the PR adds or
    changes (git diff BASE HEAD), counted only where the compiler emitted code;
  * core files: every executable line of those same files;
  * project baseline and UI files: all windows/src files, and the UI files on their own.
A line is covered when any module that compiled it executed it (hits are merged across the
test binaries). Exits 1 when changed-core coverage is below --threshold, 2 on bad input.
"""
import argparse
import collections
import pathlib
import re
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

UI_FILES = {
    'App.cpp', 'App.h', 'PopupWindow.cpp', 'PopupWindow.h', 'SettingsWindow.cpp', 'SettingsWindow.h',
    'TranslationProfilesWindow.cpp', 'TranslationProfilesWindow.h', 'TrayIcon.cpp', 'TrayIcon.h',
    'SelectionIcon.cpp', 'SelectionIcon.h', 'SelectionMonitor.cpp', 'SelectionMonitor.h',
    'Hooks.cpp', 'Hooks.h', 'main.cpp', 'UITheme.h', 'UITheme.cpp',
}
SELF_TEST_FILES = {
    'SelfTest.cpp', 'SelfTest.h', 'RuntimeSelfTest.cpp', 'RuntimeSelfTest.h', 'ProfileSelfTest.cpp',
}
SOURCE_SUFFIXES = ('.cpp', '.h')


def source_name(filename: str) -> str | None:
    """Basename of a file directly under windows/src, or None for anything else."""
    parts = re.split(r'[\\/]', filename)
    if len(parts) >= 3 and parts[-3].lower() == 'windows' and parts[-2].lower() == 'src':
        return parts[-1]
    return None


def read_hits(cobertura: pathlib.Path) -> dict[str, dict[int, bool]]:
    """basename -> {line: covered}, merged across every module that compiled the file."""
    lines: dict[str, dict[int, bool]] = collections.defaultdict(dict)
    root = ElementTree.parse(cobertura).getroot()
    for cls in root.iter('class'):
        name = source_name(cls.get('filename', ''))
        if not name:
            continue
        for line in cls.iter('line'):
            number = int(line.get('number'))
            covered = int(line.get('hits', '0')) > 0
            lines[name][number] = lines[name].get(number, False) or covered
    return lines


def changed_lines(base: str, repo: pathlib.Path) -> dict[str, set[int]]:
    """basename -> added/changed line numbers in HEAD, for windows/src files."""
    diff = subprocess.run(['git', 'diff', '--unified=0', '--no-color', base, 'HEAD', '--', 'windows/src'],
                          cwd=repo, check=True, capture_output=True, text=True, encoding='utf-8').stdout
    changed: dict[str, set[int]] = collections.defaultdict(set)
    current = None
    for row in diff.splitlines():
        if row.startswith('+++ '):
            path = row[4:].strip()
            current = None if path == '/dev/null' else pathlib.PurePosixPath(path).name
            if current and not current.endswith(SOURCE_SUFFIXES):
                current = None
            continue
        match = re.match(r'@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@', row)
        if match and current:
            start, count = int(match.group(1)), int(match.group(2) or '1')
            changed[current].update(range(start, start + count))
    return changed


def ratio(covered: int, total: int) -> str:
    return 'n/a' if total == 0 else f'{100.0 * covered / total:.1f}%'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--cobertura', required=True, type=pathlib.Path)
    parser.add_argument('--repo', default='.', type=pathlib.Path)
    parser.add_argument('--base', required=True, help='git revision the PR is compared with, e.g. origin/main')
    parser.add_argument('--threshold', type=float, default=80.0)
    parser.add_argument('--summary', type=pathlib.Path, help='Markdown summary to write (appended)')
    args = parser.parse_args()
    if not args.cobertura.is_file():
        print(f'missing Cobertura file: {args.cobertura}')
        return 2

    hits = read_hits(args.cobertura)
    changed = changed_lines(args.base, args.repo)
    core = sorted(name for name in changed if name not in UI_FILES and name not in SELF_TEST_FILES)

    rows = []
    unmeasured = []  # core .cpp files no covered module compiled: never counted as covered
    changed_total = changed_covered = file_total = file_covered = 0
    for name in core:
        executable = hits.get(name, {})
        changed_exec = [line for line in changed[name] if line in executable]
        c_cov = sum(1 for line in changed_exec if executable[line])
        f_cov = sum(1 for covered in executable.values() if covered)
        changed_total += len(changed_exec)
        changed_covered += c_cov
        file_total += len(executable)
        file_covered += f_cov
        note = ''
        if not executable:
            note = ' (no code recorded: header without code)'
            if name.endswith('.cpp'):
                unmeasured.append(name)
                note = ' (UNMEASURED: compiled into no covered binary)'
        rows.append(f'| {name}{note} | {c_cov}/{len(changed_exec)} | {ratio(c_cov, len(changed_exec))} '
                    f'| {f_cov}/{len(executable)} | {ratio(f_cov, len(executable))} |')

    def totals(names):
        total = covered = 0
        for name in names:
            executable = hits.get(name, {})
            total += len(executable)
            covered += sum(1 for value in executable.values() if value)
        return covered, total

    project = [n for n in hits if n not in SELF_TEST_FILES]
    ui = [n for n in hits if n in UI_FILES]
    p_cov, p_total = totals(project)
    u_cov, u_total = totals(ui)
    changed_pct = 0.0 if changed_total == 0 else 100.0 * changed_covered / changed_total
    passed = changed_total > 0 and changed_pct >= args.threshold and not unmeasured

    report = [
        '## Translation-profiles line coverage (OpenCppCoverage, Debug + PDB)',
        '',
        f'- Changed core lines: **{changed_covered}/{changed_total} = {ratio(changed_covered, changed_total)}** '
        f'(gate: >= {args.threshold:.0f}%: {"met" if passed else "NOT met"})',
        *([f'- Unmeasured core sources (gate fails): {", ".join(unmeasured)}'] if unmeasured else []),
        f'- Core files, all lines: {file_covered}/{file_total} = {ratio(file_covered, file_total)}',
        f'- Project baseline (windows/src without self-test files): {p_cov}/{p_total} = {ratio(p_cov, p_total)}',
        f'- UI files: {u_cov}/{u_total} = {ratio(u_cov, u_total)} (UI is driven by the end-to-end jobs, '
        'which do not run under coverage)',
        '',
        f'Core = windows/src files changed against `{args.base}`, excluding UI and self-test files.',
        '',
        '| File | Changed lines covered | % | All lines covered | % |',
        '| --- | --- | --- | --- | --- |',
        *rows,
        '',
    ]
    text = '\n'.join(report)
    print(text)
    if args.summary:
        with args.summary.open('a', encoding='utf-8') as out:
            out.write(text + '\n')
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
