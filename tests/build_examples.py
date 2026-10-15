"""Build real examples with their declared feature unions; verify the windowless image pipeline."""

import array
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

from PIL import Image

from build_modules import PACKAGE_FEATURES, ROOT, run, verify_dependencies, verify_sources
from build_tools import REPORT


CASES = {
    'window': ('basics.window', {'example-basics'}, {'shader', 'graph', 'world', 'editor_gui'}),
    'vri': ('basics.vri', {'example-basics'}, {'graph', 'world', 'editor_gui', 'asset_import'}),
    'graph': ('basics.graph', {'example-basics'}, {'graph_json', 'world', 'editor_gui', 'asset_import'}),
    'mesh': ('basics.mesh-shading', {'example-basics'}, {'graph', 'world', 'editor_gui', 'asset_import'}),
    'offline': ('offline', {'example-offline'}, {'window', 'world', 'editor_gui', 'asset_import', 'rendering'}),
    'ui': ('ui', {'example-ui'}, {'world', 'api_host', 'asset_import', 'game_shader'}),
    'ui_native': ('ui.native', {'example-ui'}, {'asset_import', 'game_shader', 'script_host'}),
    'scene': ('scene', {'example-scene'}, {'world', 'api_host', 'script_host', 'research'}),
    'scene_helmet': ('scene.helmet', {'example-scene'}, {'world', 'script_host', 'research'}),
    'ray_triangle': ('ray.triangle', {'example-ray'}, {'rendering', 'asset_import', 'world', 'game_shader'}),
    'ray_cornell': ('ray.cornell', {'example-ray'}, {'rendering', 'world', 'game_shader'}),
    'ray_query': ('ray.query', {'example-ray'}, {'blit', 'rendering', 'world', 'game_shader'}),
    'xr_triangle': ('xr.triangle', {'example-xr'}, {'rendering', 'asset_import', 'world', 'game_shader'}),
    'xr_sponza': ('xr.sponza', {'example-xr'}, {'world', 'script_host', 'research'}),
    'shader': ('shader', {'example-shader'}, {'world', 'asset_import', 'script_host', 'research'}),
    'research': ('research', {'example-research'}, {'script_host', 'research', 'research_reference', 'editor'}),
    'scripting': ('scripting', {'example-scripting'}, {'rendering', 'asset_import', 'research', 'editor'}),
    'mixed': ('basics.window,offline,ui', {'example-basics', 'example-offline', 'example-ui'}, {'world', 'asset_import'}),
}


def pfm(path):
    with path.open('rb') as stream:
        assert stream.readline() == b'PF\n'
        size = tuple(int(value) for value in stream.readline().split())
        scale = float(stream.readline())
        values = array.array('f')
        values.frombytes(stream.read())
    if (scale < 0) != (sys.byteorder == 'little'):
        values.byteswap()
    assert len(values) == size[0] * size[1] * 3
    return size, values


def offline(binary, directory, env):
    def invoke(label, arguments, *, succeeds=True):
        output = run([str(binary), *arguments], directory / (label + '.log'), succeeds=succeeds, env=env, cwd=ROOT)
        if succeeds and ('[error]' in output or 'Validation Error' in output):
            raise RuntimeError(output)
        return output

    for label, options in (
        ('direct', []), ('graph', ['--graph', 'examples/research/color_gain.vgraph']), ('full', ['--gain', '1']),
    ):
        invoke(label, ['--frames', '2', '--output', str(directory / label), *options])
    for name in ('linear.pfm', 'display.png'):
        assert (directory / 'direct' / name).read_bytes() == (directory / 'graph' / name).read_bytes(), name
    size, half = pfm(directory / 'direct/linear.pfm')
    _, full = pfm(directory / 'full/linear.pfm')
    assert size == (129, 97) and max(full) > 0.9 and full[:3] == array.array('f', [0, 0, 0])
    assert all(abs(a - b * 0.5) < 0.001 for a, b in zip(half, full))
    image = Image.open(directory / 'direct/display.png').convert('RGBA')
    assert image.size == size and image.getpixel((0, 0)) == (0, 0, 0, 255)
    # PFM stores the bottom row first; validate the real display conversion, not just identical files.
    for y in range(size[1]):
        for x in range(size[0]):
            index = ((size[1] - 1 - y) * size[0] + x) * 3
            expected = []
            for value in half[index:index + 3]:
                srgb = value * 12.92 if value <= 0.0031308 else 1.055 * value ** (1 / 2.4) - 0.055
                expected.append(round(srgb * 255))
            actual = image.getpixel((x, y))
            assert all(abs(a - b) <= 2 for a, b in zip(actual[:3], expected)), (x, y, actual, expected)
            assert actual[3] == 255
    bad = directory / 'bad.vgraph'
    bad.write_text('{"format":"vultra.graph","version":1,"passes":[],"edges":[],"outputs":["missing.color"]}')
    output = directory / 'rejected'
    invoke('invalid-graph', ['--graph', str(bad), '--output', str(output)], succeeds=False)
    assert not output.exists()
    invoke('invalid-gain', ['--gain', '9', '--output', str(output)], succeeds=False)
    assert not output.exists()
    invoke('invalid-frames', ['--frames', '0', '--output', str(output)], succeeds=False)
    invoke('recovery', ['--frames', '1', '--output', str(directory / 'recovery')])
    assert (directory / 'recovery/linear.pfm').read_bytes() == (directory / 'direct/linear.pfm').read_bytes()


def main():
    cases = CASES
    if len(sys.argv) > 1:
        unknown = set(sys.argv[1:]) - cases.keys()
        if unknown:
            raise ValueError(f'Unknown example cases: {sorted(unknown)}')
        cases = {name: cases[name] for name in sys.argv[1:]}
    evidence = Path(tempfile.mkdtemp(prefix='examples-', dir=ROOT / 'build/.tmp'))
    report_script = evidence / 'report.lua'
    report_script.write_text(REPORT)
    env = os.environ.copy()
    for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'VULTRA_NATIVE_PLUGIN'):
        env.pop(name, None)
    for name, (selection, binaries, forbidden) in cases.items():
        directory = evidence / name
        directory.mkdir()
        case_env = env | {'XMAKE_CONFIGDIR': str(directory)}
        run(['xmake', 'f', '-y', '-c', '-m', 'release', '-o', str(directory / 'build'),
             '--libvultra_features=foundation', '--libvultra_build_tests=n', '--libvultra_build_examples=y',
             '--libvultra_examples=' + selection, '--libvultra_window_backend=sdl3',
             '--libvultra_with_openxr=' + ('y' if name.startswith('xr_') else 'n')],
            directory / 'configure.log', env=case_env, cwd=ROOT)
        run(['xmake', 'build', '-a', '-y'], directory / 'build.log', env=case_env, cwd=ROOT)
        shutil.copy2(ROOT / 'compile_commands.json', directory / 'compile_commands.json')
        report_file = directory / 'report.json'
        run(['xmake', 'lua', str(report_script), str(report_file)], directory / 'report.log', env=case_env, cwd=ROOT)
        report = json.loads(report_file.read_text())
        features = set(report['features'])
        assert not features & forbidden, (name, features & forbidden)
        packages = set(report['packages'])
        expected = {'argparse', 'spdlog'} | {p for p, owners in PACKAGE_FEATURES.items() if features & owners}
        assert packages == expected, (name, packages, expected)
        targets = report['targets']
        actual = {target for target, entry in targets.items() if target.startswith('example-') and entry['kind'] == 'binary'}
        assert actual == binaries, (name, actual, binaries)
        assert not any(target.startswith('test-') or target in ('vultra-runtime', 'vultra-app', 'vultra-batch', 'vultra-research')
                       for target in targets), name
        sources = [str((ROOT / file).resolve()) for file in targets['vultra']['sources']]
        verify_sources(features, sources)
        if name == 'scene_helmet':
            assert 'asset-sponza' not in targets and 'asset-damaged-helmet' in targets
            assert not any('sponza' in file for file in targets['example-scene']['sources'])
        if name == 'window':
            assert 'example-common' not in targets
        if name == 'ray_query':
            assert not any('ray_tracing_app' in file for file in targets['example-ray-common']['sources'])
        if name == 'ui':
            assert 'example-native-plugin' not in targets
        for target in sorted(binaries):
            binary = (ROOT / targets[target]['file']).resolve()
            verify_dependencies(binary, features, directory / (target + '-dynamic.log'), case_env)
            help_text = run([str(binary), '--help'], directory / (target + '-help.log'), env=case_env, cwd=ROOT)
            if target in ('example-basics', 'example-scene', 'example-ray', 'example-xr'):
                group = target.removeprefix('example-')
                modes = [line.split()[0] for line in help_text.splitlines() if line.startswith('  ')]
                wanted = [id.split('.', 1)[1] for id in selection.split(',') if id.startswith(group + '.')]
                if wanted:
                    assert sorted(modes) == sorted(wanted), (name, modes, wanted)
                for mode in modes:
                    run([str(binary), mode, '--help'], directory / (mode + '-help.log'), env=case_env, cwd=ROOT)
            if target == 'example-offline':
                offline(binary, directory, case_env)
            if name == 'ui':
                text = run([str(binary), '--frames', '1'], directory / 'missing-plugin.log', succeeds=False,
                           env=case_env | {'VULTRA_NATIVE_PLUGIN': 'unused.so'}, cwd=ROOT)
                assert 'requires the ui.native' in text
        print(f'{name}: real example sources, packages, modes and CLI verified', flush=True)
    for selection, message in (('missing', 'Unknown Vultra example selection'),
                               ('xr.triangle', 'XR examples require libvultra_with_openxr=true')):
        output = run(['xmake', 'f', '-y', '-c', '--libvultra_features=foundation', '--libvultra_build_tests=n',
                      '--libvultra_build_examples=y', '--libvultra_examples=' + selection, '--libvultra_with_openxr=n'],
                     evidence / (selection + '-rejected.log'), succeeds=False,
                     env=env | {'XMAKE_CONFIGDIR': str(evidence / selection)}, cwd=ROOT)
        assert message in output, output
    print(f'Example evidence: {evidence}', flush=True)


if __name__ == '__main__':
    main()
