"""Prepare a self-contained (bundled) Rance10 payload for the -PBUNDLED_GAME build.

Read-only on the game directory and the converted movies: every payload file is a
hard link when the volume allows it, otherwise a copy. Outputs:
  work/standalone-assets/bundled-game.json   manifest consumed by BundledLauncherActivity
  work/standalone-assets/game/...            assets packaged into the APK
  work/standalone-engine-assets/             fonts/shaders/licenses copies
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
port = Path(__file__).resolve().parents[1]

game = Path(os.environ.get('RANCE10_GAME_DIR') or (port.parent / '兰斯10 决战'))
movies = Path(os.environ.get('RANCE10_MOVIES_DIR') or (port / 'work/movies-final'))
payload = Path(os.environ.get('BUNDLED_ASSETS') or (port / 'work/standalone-assets'))
engine_assets = Path(os.environ.get('BUNDLED_ENGINE_ASSETS') or (port / 'work/standalone-engine-assets'))
app_assets = port / 'android/project/app/src/main/assets'

# Only these files are part of a playable install; PC-only executables and the
# untranslated original movies are deliberately excluded.
ROOT_FILES = ['Rance10.ain', 'Rance10EX.ex', 'AliceStart.ini', 'Version.txt']
DATA_GLOBS = ['Data/*.slk']
MANUAL_GLOBS = ['Manual/*']
MOVIES = ['OP1A.apeg', 'OP1B.apeg', 'OP1C.apeg', 'ED2B.apeg']


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def place(source, target):
    """Hard-link when possible so a 3.9 GiB payload costs no extra space."""
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        target.unlink()
    try:
        os.link(source, target)
    except OSError:
        shutil.copy2(source, target)


def main():
    global game, movies, payload, engine_assets
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--minimal', action='store_true', help='tiny payload for pipeline checks')
    parser.add_argument('--game', default=str(game), help='game installation directory (read-only)')
    parser.add_argument('--movies', default=str(movies), help='directory with recompressed OP/ED movies')
    parser.add_argument('--payload', default=str(payload), help='output payload directory')
    parser.add_argument('--engine-assets', default=str(engine_assets), help='output engine assets directory')
    args = parser.parse_args()
    game = Path(args.game)
    movies = Path(args.movies)
    payload = Path(args.payload)
    engine_assets = Path(args.engine_assets)

    for required in ('Rance10.ain', 'AliceStart.ini'):
        if not (game / required).is_file():
            raise SystemExit(f'Missing {required} in {game}')
    for name in MOVIES:
        if not (movies / name).is_file():
            raise SystemExit(f'Missing converted movie {name} in {movies}')

    if payload.exists():
        for child in payload.iterdir():
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink()
    payload.mkdir(parents=True, exist_ok=True)
    (payload / 'game').mkdir(exist_ok=True)

    entries = []
    afa = sorted(p for p in game.glob('*.afa') if p.is_file())
    if args.minimal:
        afa = [p for p in afa if p.stat().st_size < 2 << 20]
    for path in afa:
        place(path, payload / 'game' / path.name)
        entries.append(dict(path=path.name, size=path.stat().st_size, sha256=sha256(path), inApk=True))

    others = []
    for name in ROOT_FILES:
        if (game / name).is_file():
            others.append(game / name)
    for pattern in DATA_GLOBS + MANUAL_GLOBS:
        others.extend(sorted(p for p in game.glob(pattern) if p.is_file()))
    for name in ([] if args.minimal else MOVIES):
        others.append(movies / name)
    for path in others:
        relative = path.relative_to(game).as_posix() if path.is_relative_to(game) else f'Movie/{path.name}'
        place(path, payload / 'game' / relative)
        entries.append(dict(path=relative, size=path.stat().st_size, sha256=sha256(path), inApk=False))

    entries.sort(key=lambda e: e['path'])
    content_id = hashlib.sha256(json.dumps(entries, sort_keys=True).encode()).hexdigest()
    manifest = dict(
        format=1,
        contentId=content_id,
        totalBytes=sum(e['size'] for e in entries),
        files=entries,
    )
    (payload / 'bundled-game.json').write_text(json.dumps(manifest, indent=1, sort_keys=True), encoding='utf-8')

    if engine_assets.exists():
        shutil.rmtree(engine_assets)
    engine_assets.mkdir(parents=True)
    for name in ('fonts', 'licenses', 'shaders'):
        shutil.copytree(app_assets / name, engine_assets / name)
    if not (payload / 'game' / 'fonts').exists():
        pass  # fonts live in standalone-engine-assets, not in the payload

    total = sum(e['size'] for e in entries)
    print(json.dumps(dict(files=len(entries), payload_bytes=total,
                          extracted_bytes=sum(e['size'] for e in entries if not e['inApk']),
                          content_id=content_id, minimal=args.minimal), ensure_ascii=False))


if __name__ == '__main__':
    main()
