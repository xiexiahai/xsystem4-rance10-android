"""Reuse CMake's spelling of verified same-repository paths without deleting caches."""
from pathlib import Path
import re


def verified_cached_paths(cache_file, source, build, toolchain):
    expected = {'CMAKE_HOME_DIRECTORY': Path(source), 'CMAKE_CACHEFILE_DIR': Path(build),
                'CMAKE_TOOLCHAIN_FILE': Path(toolchain)}
    result = {key: str(value) for key, value in expected.items()}
    cache_file = Path(cache_file)
    if not cache_file.is_file():
        return result
    values = {}
    for line in cache_file.read_text(encoding='utf-8').splitlines():
        match = re.match(r'^(CMAKE_HOME_DIRECTORY|CMAKE_CACHEFILE_DIR|CMAKE_TOOLCHAIN_FILE):[^=]+=(.*)$', line)
        if match:
            values[match[1]] = match[2]
    for key, path in expected.items():
        spelling = values.get(key)
        if not spelling:
            raise ValueError(f'Existing CMake cache lacks {key}; inspect it rather than deleting it automatically')
        try:
            same = Path(spelling).samefile(path)
        except OSError as error:
            raise ValueError(f'Cannot verify cached {key}={spelling}; restore the known path alias before building') from error
        if not same:
            raise ValueError(f'Cached {key} belongs to a different source/build/toolchain: {spelling}')
        result[key] = spelling
    return result
