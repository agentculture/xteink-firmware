"""
PlatformIO pre-build script (xteink fork): give every xteink release build a
unique CROSSPOINT_VERSION.

Upstream's gh_release env sets CROSSPOINT_VERSION to the bare
[crosspoint] version, so every release build reports the same "1.6.5" and the
OTA decision, the rolled-back record and the post-update health check cannot
tell two builds apart. The xteink_release env leaves the define out of its
build_flags; this script injects

    <[crosspoint] version>+xteink.<8-char git SHA>    e.g. 1.6.5+xteink.9a1b2c3d

into the translation units that use it (the same middleware approach as
scripts/git_branch.py, so library objects stay reusable across commits).
Only the xteink_release env is touched; gh_release and the dev envs are not.
"""

import configparser
import os
import re
import subprocess
import sys

ENV_NAME = 'xteink_release'
# Characters kept in the version: nothing that can end or escape a C string
# literal, and nothing the server's version field would reject.
_UNSAFE = re.compile(r'[^0-9A-Za-z.+_-]')


def warn(msg):
    print(f'WARNING [xteink_release_version.py]: {msg}', file=sys.stderr)


def safe(value):
    return _UNSAFE.sub('', value)


def base_version(project_dir):
    config = configparser.ConfigParser()
    config.read(os.path.join(project_dir, 'platformio.ini'), encoding='utf-8')
    if not config.has_option('crosspoint', 'version'):
        warn('No [crosspoint] version in platformio.ini; using "0.0.0"')
        return '0.0.0'
    return safe(config.get('crosspoint', 'version')) or '0.0.0'


def short_sha(project_dir):
    try:
        sha = subprocess.check_output(
            ['git', 'rev-parse', '--short=8', 'HEAD'],
            text=True, stderr=subprocess.PIPE, cwd=project_dir,
        ).strip()
    except (OSError, subprocess.CalledProcessError) as e:
        # A release without its commit cannot be told apart from another one.
        raise SystemExit(f'ERROR [xteink_release_version.py]: cannot read the git SHA: {e}') from e
    sha = safe(sha)
    if not re.fullmatch(r'[0-9a-f]{8,}', sha):
        raise SystemExit(f'ERROR [xteink_release_version.py]: unexpected git SHA {sha!r}')
    return sha[:8]


def release_version(project_dir):
    return f'{base_version(project_dir)}+xteink.{short_sha(project_dir)}'


def inject_version(env):
    if env['PIOENV'] != ENV_NAME:
        return
    version_string = release_version(env['PROJECT_DIR'])

    def add_version(build_env, node):
        if b'CROSSPOINT_VERSION' not in node.srcnode().get_contents():
            return node
        version_env = build_env.Clone()
        version_env.Append(CPPDEFINES=[('CROSSPOINT_VERSION', f'\\"{version_string}\\"')])
        return version_env.Object(node)

    env.AddBuildMiddleware(add_version)
    print(f'CrossPoint build version: {version_string}')


try:
    Import('env')           # noqa: F821  # type: ignore[name-defined]
    inject_version(env)     # noqa: F821  # type: ignore[name-defined]
except NameError:
    # Run directly: print the version the release env would get.
    print(release_version(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
