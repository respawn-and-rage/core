#!/usr/bin/env python3
"""Reject private assets and unapproved paths throughout public Git history."""
import hashlib
import subprocess
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
# The previous project filename remains allowed only to audit existing Git history.
ROOT_FILES = {'fdm.uproject', 'RespawnAndRage.uproject', 'README.md', 'AGENTS.md', 'CLAUDE.md', '.gitignore', '.gitattributes',
              '.gitmodules', 'assets.json', 'ASSETS_REQUIRED.md', 'CODE_EXPORT.json', 'Branding/README.md'}
EXTENSIONS = {'Source': {'.cpp', '.h', '.cs', '.mm'}, 'Config': {'.ini'},
              'scripts': {'.py', '.command', '.sh', '.ps1'}, 'docs': {'.md', '.txt', '.json', '.csv'}}
SUBMODULES = {'Assets/characters', 'Assets/weapons', 'Assets/maps'}
# Owned extension snippets used to prepare the ignored, locally licensed plugin.
# Keep this exact: generated plugin and arbitrary C++ under scripts stay private.
PUBLIC_EOS_GUEST_TEXT = {
    'scripts/eos-guest/RRDeviceIdLogin.inl',
    'scripts/eos-guest/RRDeviceIdRefresh.inl',
    'scripts/eos-guest/RRDeviceIdUserInfo.inl',
    'scripts/eos-guest/RRGuestEOS.h',
    'scripts/eos-guest/RRGuestProbe.inl',
}
# Owner-supplied runtime copy and its documentation; keep all other Config
# JSON/Markdown paths forbidden and apply the usual text/LFS/mode checks.
PUBLIC_GAMEPLAY_TEXT = {
    'Config/GameplayMessages/respawn-and-rage-gameplay-messages.en-US.json',
    'Config/GameplayMessages/README.md',
}
# This parameterized deployment template contains no environment configuration.
PUBLIC_SERVER_CHAT_TEMPLATE = 'scripts/server-chat/stack.yaml'
# Exact text paths approved alongside the menu font and credits. Do not allow
# the whole Branding tree: licensed game assets still belong in submodules.
PUBLIC_BRANDING_TEXT = {
    'Branding/Credits.txt',
    'Branding/Fonts/OFL.txt',
    'Branding/Fonts/README.md',
    'Branding/Fonts/Teko/CREDITS.txt',
    'Branding/Fonts/Teko/README.md',
    'Branding/Fonts/Teko/SourceInstructions.it.md',
    'Branding/Fonts/Teko/ThirdPartyLicenses/Teko-OFL.txt',
    'Branding/Fonts/Teko/download_teko.py',
    'Branding/Fonts/Teko/make_static.py',
}
# User-supplied game branding and its prepared derivatives, 16 October 2026.
# Pin both path and bytes: this is not permission for arbitrary binary assets.
PUBLIC_BRANDING = {
    # Derived from the existing user-provided icon; Retina packaging, 18 October.
    'Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/Contents.json': '13b1c7461192863c9f3ab3ceab313acd6f6531898360983495bf13d48dfc967f',
    'Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/icon_128x128.png': 'bbf54f2e642f8e09ed47c56094f774faf00fe21aec5c33c80b8c722852682563',
    'Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/icon_128x128@2x.png': '0df29e9caa13b07a55787e18d8072476c14f30bb92459fa942dfc04000584a6a',
    'Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/icon_16x16.png': '7834f7ca94e62b732a280b396f6154c7e588fa2c1a9451b732ed2802ca13a38d',
    'Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/icon_16x16@2x.png': '43a582b51dafd11441c5f9f35e22c593bedd8c8d14b7be65190b23a9e7bd22aa',
    'Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/icon_256x256.png': '0df29e9caa13b07a55787e18d8072476c14f30bb92459fa942dfc04000584a6a',
    'Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/icon_256x256@2x.png': 'a13f7be156e1e0c17a892b50926a3c2a16061f239ef4d9dc45936941c0699924',
    'Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/icon_32x32.png': '43a582b51dafd11441c5f9f35e22c593bedd8c8d14b7be65190b23a9e7bd22aa',
    'Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/icon_32x32@2x.png': 'd109f87fc24b9cb08edd3412e31773e94f763f78b70fb118d2e721c373ea8831',
    'Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/icon_512x512.png': 'a13f7be156e1e0c17a892b50926a3c2a16061f239ef4d9dc45936941c0699924',
    'Build/Mac/Resources/Assets.xcassets/AppIcon.appiconset/icon_512x512@2x.png': '10320d04e40b2685e42e012586f23189a729a9dc34c425fd590492d58c2126d5',
    'Build/Mac/Resources/Assets.xcassets/Contents.json': 'f5fb0e1f3a8142892ed092efb5955c1c2cc537003a89ad7bbdb9dade7fe8985f',
    'Build/Mac/Resources/Info.Template.plist': '8e059740fec62984533d8630ca6dcee81ff7f19206616fc701691fea157d0666',
    # Approved menu fonts, with complete OFL notices at the exact text paths above.
    'Branding/Fonts/BarlowCondensed-SemiBold.ttf': '7b619d14bc2327509a9ef32b0890f709626f7ecc9ff61191c2a4314c5499d2d9',
    'Branding/Fonts/Teko/Fonts/Teko-SemiBold.ttf': '198159cd0a8faa1337822d7c5d10fcdbc01f28b1628a8db9e59fae5c1fd0f092',
    'Branding/Fonts/Teko/Fonts/Teko-Variable.ttf': 'd1321889f262bbbff632e7976349853399cd097b6f382d4b19790c915c13c1ae',
    'Branding/Originals/icon.png': '02af44bd47fd2183c5a48135377c0fb912d8ccdc012b5dfcc8b5bd1aca5736d5',
    'Branding/Originals/logo.png': 'c59c323dd69b356a575331386155e3e597e8174d2a3b630246cb49047012ebdf',
    'Branding/Originals/splash.png': '205a1e94c9be875585c24494a968e2bafb5753d419f487392a45bc8f9dca6828',
    'Branding/background.jpg': 'b08ac4f5e52748f2a22f53cf28dfcdeee63fb1efef3704ce9cb6f4c192d047b3',
    'Branding/icon.png': '10320d04e40b2685e42e012586f23189a729a9dc34c425fd590492d58c2126d5',
    'Branding/logo.png': '1abb569b4498bad262d43523c638d1d2355e8e847b2af1fea15ed577088446a2',
    'Build/Mac/Application.icns': '2b24c5fc260ecb241ec2c4a67c3ac0779f77110f3db6ca7aa2573853bdf079c7',
    'Build/Windows/Application.ico': '2629e2aa33d8408b7bcdbe154a6dd76bab2bfd2b97b8899fd52456590eb6e3a3',
    'Content/Splash/EdSplash.png': '2ee026ee51b2a4fd771e31f9440c4051c5240521fab56a316553a7d5733d1d06',
    'Content/Splash/Icon.png': '1a6386df237dfebda6c7ca6daf0a9cf7b4d925e2f4e7a8e3231d7f10eea618a8',
    'Content/Splash/Splash.png': '2ee026ee51b2a4fd771e31f9440c4051c5240521fab56a316553a7d5733d1d06',
}

# The verifier audits all history. Keep reviewed older bytes accepted as well
# as the current plist, which adds the local-network usage description.
PUBLIC_BRANDING_HISTORY = {
    'Build/Mac/Resources/Info.Template.plist': {
        '62d6341c5d79a25e70b46413ecffeed4b4aa457869129adbaaeda01c43f4798a',
    },
}


def read_blob_facts(batch, oid):
    """Read one framed batch response; retain only its hash and text classification."""
    batch.stdin.write(oid.encode('ascii') + b'\n')
    batch.stdin.flush()
    header = batch.stdout.readline().split()
    if len(header) != 3 or header[0] != oid.encode('ascii') or header[1] != b'blob':
        raise RuntimeError('Cannot read Git blob ' + oid)
    remaining = int(header[2])
    digest = hashlib.sha256()
    is_binary = False
    first = True
    while remaining:
        chunk = batch.stdout.read(min(remaining, 1024 * 1024))
        if not chunk:
            raise RuntimeError('Truncated Git blob ' + oid)
        digest.update(chunk)
        is_binary |= b'\0' in chunk or (first and chunk.startswith(b'version https://git-lfs.github.com/spec/v1'))
        first = False
        remaining -= len(chunk)
    if batch.stdout.read(1) != b'\n':
        raise RuntimeError('Invalid Git batch response for ' + oid)
    return digest.hexdigest(), is_binary


def verify(root):
    revisions = subprocess.check_output(['git', 'rev-list', '--all'], cwd=root, text=True).splitlines()
    failures = []
    count = 0
    # Cache content facts only: path/mode approval still applies to every entry.
    facts = {}
    with subprocess.Popen(['git', 'cat-file', '--batch'], cwd=root,
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE) as batch:
        try:
            for revision in revisions:
                tree = subprocess.check_output(['git', 'ls-tree', '-rz', revision], cwd=root).split(b'\0')
                for item in tree:
                    if not item:
                        continue
                    info, raw_path = item.split(b'\t', 1)
                    mode, kind, oid = info.decode().split()
                    name = raw_path.decode()
                    path = Path(name)
                    if mode == '160000' and name in SUBMODULES:
                        continue
                    if name in PUBLIC_BRANDING and kind == 'blob' and mode == '100644':
                        if oid not in facts:
                            facts[oid] = read_blob_facts(batch, oid)
                        digest, is_binary = facts[oid]
                        approved_hashes = {PUBLIC_BRANDING[name]} | PUBLIC_BRANDING_HISTORY.get(name, set())
                        if digest not in approved_hashes:
                            failures.append(revision[:8] + ': unapproved branding content ' + name)
                        count += 1
                        continue
                    allowed = (name in ROOT_FILES or name in PUBLIC_BRANDING_TEXT or name in PUBLIC_GAMEPLAY_TEXT or
                               name in PUBLIC_EOS_GUEST_TEXT or name == PUBLIC_SERVER_CHAT_TEMPLATE or
                               (path.parts[0] in EXTENSIONS and path.suffix in EXTENSIONS[path.parts[0]]))
                    if kind != 'blob' or mode not in ('100644', '100755') or not allowed:
                        failures.append(revision[:8] + ': forbidden path/mode ' + name)
                        continue
                    if oid not in facts:
                        facts[oid] = read_blob_facts(batch, oid)
                    digest, is_binary = facts[oid]
                    if is_binary:
                        failures.append(revision[:8] + ': binary/LFS blob ' + name)
                    count += 1
        finally:
            batch.stdin.close()
        code = batch.wait()
        if code:
            raise subprocess.CalledProcessError(code, batch.args)
    if not revisions:
        failures.append('No committed history to verify')
    if failures:
        raise RuntimeError('\n'.join(failures))
    return len(revisions), count


if __name__ == '__main__':
    try:
        commits, blobs = verify(ROOT)
        print(f'PUBLIC_TREE_PASS: {commits} commits, {blobs} approved entries; private assets remain gitlinks only')
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
