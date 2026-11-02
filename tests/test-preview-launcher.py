#!/usr/bin/env python3
"""Exercise the actual preview launcher with a harmless stand-in executable."""
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / 'scripts/run-cachyos-oxp3.sh'


class PreviewLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.home = self.base / 'home'
        self.home.mkdir()
        self.bin = self.base / 'bin'
        self.bin.mkdir()
        # Only platform detection is replaced; no application, decoder or network runs.
        (self.bin / 'uname').write_text('#!/bin/sh\nprintf "Linux\\n"\n')
        (self.bin / 'uname').chmod(0o755)
        self.dummy = self.bin / 'moonlight-stand-in'
        self.dummy.write_text(
            '#!/usr/bin/env python3\nimport json,os,sys\n'
            'from pathlib import Path\n'
            'Path(os.environ["PREVIEW_TEST_RESULT"]).write_text(json.dumps({'
            '"args":sys.argv[1:],"cwd":os.getcwd(),'
            '"xdg":{k:os.environ[k] for k in '
            '("XDG_CONFIG_HOME","XDG_CACHE_HOME","XDG_DATA_HOME","TMPDIR")}}))\n'
        )
        self.dummy.chmod(0o755)
        self.state = self.home / 'data/moonlight-oxp3-preview'
        self.result = self.base / 'result.json'
        self.env = dict(os.environ, HOME=str(self.home),
                        PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        XDG_DATA_HOME=str(self.home / 'data'),
                        MOONLIGHT_OXP3_BINARY=str(self.dummy),
                        PREVIEW_TEST_RESULT=str(self.result))
        self.env.pop('MOONLIGHT_OXP3_STATE_DIR', None)
        self.env.pop('MOONLIGHT_OXP3_CONFIG_SOURCE', None)
        self.source = (self.home / '.var/app/com.moonlight_stream.Moonlight/'
                       'config/Moonlight Game Streaming Project/Moonlight.conf')

    @property
    def target(self):
        return self.state / 'config/Moonlight Game Streaming Project/Moonlight.conf'

    def run_preview(self, *args, success=True):
        result = subprocess.run(['bash', str(LAUNCHER), *args], env=self.env,
                                text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def install_fixture(self):
        self.source.parent.mkdir(parents=True)
        data = (b'[General]\r\nuniqueid=FixtureIdentity\r\n'
                b'certificate=@ByteArray(Mock\\nCertificate)\r\n'
                b'key=@ByteArray(Mock\\0Key)\r\nwidth=1920\r\n'
                b'CustomCase=KeepThisExactly\r\nnativeresolution=false\r\n'
                b'nativefps=false\r\n[Hosts]\r\nHostCase=@Variant(Fixture)\r\n')
        self.source.write_bytes(data)
        self.source.chmod(0o600)
        return data

    def test_import_preserves_identity_case_and_other_bytes(self):
        data = self.install_fixture()
        self.run_preview('stream', 'host with spaces', 'Desktop')
        self.assertEqual(self.source.read_bytes(), data)
        self.assertEqual(self.target.read_bytes(), data.replace(
            b'nativeresolution=false', b'nativeresolution=true').replace(
            b'nativefps=false', b'nativefps=true'))
        self.assertEqual(stat.S_IMODE(self.target.stat().st_mode), 0o600)
        for path in (self.state, self.state / 'config', self.state / 'cache',
                     self.state / 'data', self.state / 'tmp', self.target.parent):
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o700)
        launched = json.loads(self.result.read_text())
        self.assertEqual(launched['args'], ['stream', 'host with spaces', 'Desktop'])
        self.assertEqual(launched['cwd'], str(self.state.resolve()))
        self.assertEqual(launched['xdg'], {
            'XDG_CONFIG_HOME': str(self.state / 'config'),
            'XDG_CACHE_HOME': str(self.state / 'cache'),
            'XDG_DATA_HOME': str(self.state / 'data'),
            'TMPDIR': str(self.state / 'tmp')})

    def test_later_launch_preserves_private_changes_and_does_not_reimport(self):
        self.install_fixture()
        self.run_preview()
        private = self.target.read_bytes().replace(b'nativefps=true', b'nativefps=false')
        self.target.write_bytes(private)
        self.source.write_bytes(b'[General]\nuniqueid=DifferentSource\n')
        self.run_preview()
        self.assertEqual(self.target.read_bytes(), private)

    def test_new_profile_seeds_only_native_preferences(self):
        self.run_preview()
        self.assertEqual(self.target.read_bytes(),
                         b'[General]\nnativeresolution=true\nnativefps=true\n\n')
        self.assertFalse(self.source.exists())

    def test_missing_native_keys_are_inserted_in_general_section(self):
        self.source.parent.mkdir(parents=True)
        data = b'[General]\nuniqueid=Keep\n[Hosts]\nCase=Keep\n'
        self.source.write_bytes(data)
        self.run_preview()
        self.assertEqual(self.target.read_bytes(),
                         b'[General]\nuniqueid=Keep\nnativeresolution=true\n'
                         b'nativefps=true\n[Hosts]\nCase=Keep\n')
        self.assertEqual(self.source.read_bytes(), data)

    def test_linked_destination_cannot_rewrite_source_profile(self):
        original = self.install_fixture()
        self.target.parent.mkdir(parents=True)
        self.target.symlink_to(self.source)
        self.run_preview(success=False)
        self.assertEqual(self.source.read_bytes(), original)
        self.assertFalse(self.result.exists())

    def test_state_inside_flatpak_is_refused(self):
        original = self.install_fixture()
        self.env['MOONLIGHT_OXP3_STATE_DIR'] = str(self.source.parents[2])
        self.run_preview(success=False)
        self.assertEqual(self.source.read_bytes(), original)
        self.assertFalse(self.result.exists())


if __name__ == '__main__':
    unittest.main()
