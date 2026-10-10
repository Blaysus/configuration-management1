import calendar
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import zipfile
import tempfile

ROOT = Path(__file__).resolve().parent
TEMP = tempfile.TemporaryDirectory(prefix='prac1-tests-')
DATA = Path(TEMP.name)
EXE = DATA / 'emulator'
TOTAL = 0


def archive(name, entries, compression=zipfile.ZIP_DEFLATED):
    path = DATA / name
    with zipfile.ZipFile(path, 'w', compression=compression) as output:
        for key, value in entries.items():
            output.writestr(key, value)
    return path


MINIMAL = archive('minimal.zip', {})
FILES = archive('files.zip', {
    'one.txt': 'one\ntwo\nthree\n', 'empty.txt': '',
    '.hidden': 'hidden\n', 'bytes.bin': bytes(range(256)),
    'long.txt': ''.join(f'line {i}\n' for i in range(1, 16)),
    'last.txt': 'no newline', 'empty/': '',
})
DEEP = archive('deep.zip', {'a/b/c/task.txt': 'deep file\n', 'top.txt': 'top\n'})
STORED = archive('stored.zip', {'file.txt': 'stored\n'}, zipfile.ZIP_STORED)


def check(label, args=(), commands='exit\n', contains=(), absent=(), code=0, exact=None):
    global TOTAL
    env = dict(os.environ, REPL_FOLDER='/docs', REPL_EMPTY='')
    env.pop('REPL_MISSING', None)
    result = subprocess.run([str(EXE), *map(str, args)], input=commands,
                            text=True, capture_output=True, env=env, timeout=5)
    output = result.stdout + result.stderr
    assert result.returncode == code, (label, result.returncode, output)
    for value in contains:
        assert value in output, (label, 'нет строки', value, output)
    for value in absent:
        assert value not in output, (label, 'лишняя строка', value, output)
    if exact is not None:
        assert result.stdout == exact, (label, result.stdout, exact)
    TOTAL += 1
    print('OK:', label)
    return output


def config():
    check('без параметров', contains=['VFS: по умолчанию (в памяти)', 'скрипт: не задан'])
    check('--vfs', ['--vfs', DEEP], contains=[f'VFS: {DEEP}', 'deep.zip:/$'])
    script = DATA / 'script with spaces.txt'
    script.write_text('// комментарий\nls // вывод списка\nunknown\nuname\n', encoding='utf-8')
    check('--script и продолжение REPL', ['--script', script],
          contains=['// комментарий', 'docs/', 'Ошибка: неизвестная команда', os.uname().sysname])
    check('оба параметра', ['--script', script, '--vfs', DEEP], contains=['a/', 'top.txt'])
    check('--help', ['--help'], contains=['Запуск: emulator'])
    for args in [['--vfs'], ['--script'], ['--bad'], ['--vfs', ''], ['--script', '--vfs'],
                 ['--vfs', DEEP, '--vfs', DEEP], ['--script', DATA / 'missing.txt']]:
        check('ошибка параметров ' + str(args), args, contains=['Ошибка запуска:'], code=1)
    script.write_text('exit\nunknown\n', encoding='utf-8')
    check('exit внутри скрипта', ['--script', script], absent=['unknown'])
    check('EOF', commands='', contains=['vfs22:/$'])
    check('пустые строки и неизвестная переменная', commands='\ncd $REPL_MISSING\nexit now\nexit\n',
          contains=['пустой путь', 'exit не принимает аргументы', 'Завершение работы.'])


def vfs():
    before = {p: hashlib.sha256(p.read_bytes()).digest() for p in DATA.glob('*.zip')}
    check('минимальная VFS', ['--vfs', MINIMAL], 'ls\nexit\n',
          exact=f'VFS: {MINIMAL}\nСтартовый скрипт: не задан\n'
                'minimal.zip:/$ minimal.zip:/$ Завершение работы.\n')
    check('несколько файлов и двоичный файл', ['--vfs', FILES], 'ls -a\nhead bytes.bin\nexit\n',
          contains=['one.txt', '.hidden', 'двоичный файл'])
    check('три уровня, неявные папки', ['--vfs', DEEP],
          'cd a/b/c\nhead task.txt\ncd ../../..\nls\nexit\n', contains=['deep file', 'a/', 'top.txt'])
    check('ZIP без сжатия', ['--vfs', STORED], 'head file.txt\nexit\n', contains=['stored\n'])
    check('VFS по умолчанию', commands='head /docs/course/lab/task.txt\nexit\n', contains=['Level 3'])
    check('архив не найден', ['--vfs', DATA / 'missing.zip'], code=1, contains=['открыть VFS'])
    broken = DATA / 'broken.zip'
    broken.write_bytes(b'not a zip')
    check('неверный формат', ['--vfs', broken], code=1, contains=['не является ZIP'])
    broken.write_bytes(DEEP.read_bytes()[:-5])
    check('обрезанный ZIP', ['--vfs', broken], code=1, contains=['Ошибка запуска'])
    bad = bytearray(STORED.read_bytes())
    bad[bad.index(b'stored\n')] ^= 1
    broken.write_bytes(bad)
    check('контрольная сумма', ['--vfs', broken], code=1, contains=['контрольная сумма'])
    for name, entries in [('escape.zip', {'../file': 'bad'}),
                           ('absolute.zip', {'/file': 'bad'}),
                           ('conflict.zip', {'a': 'file', 'a/b': 'bad'})]:
        path = archive(name, entries)
        check(name, ['--vfs', path], code=1, contains=['Ошибка запуска'])
    for path, digest in before.items():
        assert hashlib.sha256(path.read_bytes()).digest() == digest, 'ZIP изменился'
    assert not (ROOT / 'a').exists(), 'Данные ZIP извлечены на диск'
    print('OK: исходные ZIP не изменились, дерево VFS не извлекалось')


def commands():
    check('ls: текущая папка, файл, скрытые', ['--vfs', FILES],
          'ls\nls one.txt\nexit\n', contains=['one.txt'], absent=['.hidden'])
    check('ls -a', ['--vfs', FILES], 'ls -a /\nexit\n', contains=['.hidden'])
    check('cd: абсолютные, относительные пути, точка и корень',
          commands='cd /docs\ncd course/lab\ncd .\ncd ..\ncd\ncd ../../..\nexit\n',
          contains=['vfs22:/docs/course/lab$', 'vfs22:/docs/course$', 'vfs22:/$'], absent=['Ошибка:'])
    check('переменные окружения', commands='cd $REPL_FOLDER\nhead $REPL_FOLDER/info.txt\nexit\n',
          contains=['vfs22:/docs$', 'Hello from VFS'])
    check('пустая папка', ['--vfs', FILES], 'ls empty\nexit\n',
          exact=f'VFS: {FILES}\nСтартовый скрипт: не задан\nfiles.zip:/$ files.zip:/$ Завершение работы.\n')
    check('head по умолчанию', ['--vfs', FILES], 'head long.txt\nexit\n',
          contains=['line 10\n'], absent=['line 11\n'])
    check('head -n и несколько файлов', ['--vfs', FILES], 'head -n 1 one.txt last.txt\nexit\n',
          contains=['==> one.txt <==\none\n', '==> last.txt <==\nno newline\n'], absent=['two\n'])
    check('head пустого файла и ноль строк', ['--vfs', FILES], 'head empty.txt\nhead -n 0 one.txt\nexit\n',
          absent=['one\n', 'Ошибка:'])
    check('head больше длины', ['--vfs', FILES], 'head -n 99 one.txt\nexit\n', contains=['three\n'])
    for month, year in [(2, 2024), (2, 1900), (2, 2000), (1, 1), (12, 9999)]:
        cal = calendar.TextCalendar(calendar.SUNDAY).formatmonth(year, month)
        lines = cal.splitlines()
        expected = lines[0].strip() + '\n' + '\n'.join(lines[1:]) + '\n'
        check(f'cal {month} {year}', commands=f'cal {month} {year}\nexit\n', contains=[expected])
    check('cal на год', commands='cal 2024\nexit\n', contains=['January 2024', 'December 2024'])
    import datetime
    now = datetime.datetime.now()
    check('cal текущий месяц', commands='cal\nexit\n', contains=[f'{calendar.month_name[now.month]} {now.year}'])
    info = os.uname()
    for flag, expected in [('', info.sysname), ('-s', info.sysname), ('-n', info.nodename),
                           ('-r', info.release), ('-v', info.version), ('-m', info.machine),
                           ('-a', ' '.join(info))]:
        check('uname ' + flag, commands=f'uname {flag}\nexit\n', contains=[expected])
    for command in ['ls -z', 'ls a b', 'ls /missing', 'cd one two', 'cd /readme.txt',
                    'cd /readme.txt/../docs', 'head', 'head -n', 'head -n abc /readme.txt',
                    'head -n -1 /readme.txt', 'head /docs', 'head /absent', 'head -z /readme.txt',
                    'cal 13 2024', 'cal 0', 'cal 2 nope', 'cal 1 2 3', 'uname -x', 'uname -s -m']:
        check('ошибка: ' + command, commands=command + '\nuname\nexit\n',
              contains=['Ошибка:', info.sysname, 'Завершение работы.'])
    check('этап 5 не реализован', commands='rmdir /empty\nvfs-load anything\nexit\n',
          contains=['неизвестная команда: rmdir', 'неизвестная команда: vfs-load'])


if __name__ == '__main__':
    groups = {'config': config, 'vfs': vfs, 'commands': commands}
    try:
        subprocess.run([
            os.environ.get('CXX', 'clang++'), '-std=c++17',
            '-Wall', '-Wextra', '-Wpedantic', str(ROOT / 'main.cpp'),
            '-lz', '-o', str(EXE),
        ], check=True)
        selected = sys.argv[1:] or list(groups)
        for name in selected:
            if name not in groups:
                raise SystemExit('Группы тестов: config, vfs, commands')
            groups[name]()
        print(f'Пройдено сценариев: {TOTAL}')
    finally:
        TEMP.cleanup()
