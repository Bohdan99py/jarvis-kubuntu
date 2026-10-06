import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result
update = module('update')
voice = module('voice')

class Helpers(unittest.TestCase):
    def setUp(self):
        self.asset = dict(name='jarvis_0.7.0_ubuntu-26.04_amd64.deb', size=3,
                          digest='sha256:' + hashlib.sha256(b'deb').hexdigest(),
                          browser_download_url='https://github.com/Bohdan99py/jarvis-kubuntu/releases/download/v0.7.0/jarvis_0.7.0_ubuntu-26.04_amd64.deb')
        self.release = dict(tag_name='v0.7.0', assets=[self.asset])
    def test_release_selection(self):
        self.assertEqual(update.select_asset(self.release,'0.6.0','ubuntu-26.04','amd64'),self.asset)
        self.assertIsNone(update.select_asset(self.release,'0.7.0','ubuntu-26.04','amd64'))
        self.assertIsNone(update.select_asset(self.release,'0.8.0','ubuntu-26.04','amd64'))
        with self.assertRaises(ValueError):update.select_asset(self.release,'0.6.0','ubuntu-24.04','amd64')
    def test_untrusted_metadata(self):
        self.asset['digest']=''
        with self.assertRaises(ValueError):update.select_asset(self.release,'0.6.0','ubuntu-26.04','amd64')
        self.setUp(); self.asset['browser_download_url']='https://example.com/update.deb'
        with self.assertRaises(ValueError):update.select_asset(self.release,'0.6.0','ubuntu-26.04','amd64')
    def test_download_integrity(self):
        class Response(io.BytesIO):url='https://release-assets.githubusercontent.com/file'
        with tempfile.TemporaryDirectory() as d:
            with patch.object(update.urllib.request,'urlopen',return_value=Response(b'deb')):
                path=update.download(self.asset,Path(d));self.assertEqual(path.read_bytes(),b'deb')
            path.unlink()
            with patch.object(update.urllib.request,'urlopen',return_value=Response(b'bad')):
                with self.assertRaises(ValueError):update.download(self.asset,Path(d))
            self.assertEqual(list(Path(d).iterdir()),[])
    def test_platform(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'os-release';path.write_text('ID=ubuntu\nVERSION_ID="24.04"\n')
            self.assertEqual(update.platform_tag(path),'ubuntu-24.04')
            path.write_text('ID=other\nVERSION_ID="24.04"\n')
            with self.assertRaises(ValueError):update.platform_tag(path)
    def test_archive_paths(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d); archive=d/'model.zip'
            with zipfile.ZipFile(archive,'w') as z:z.writestr('../escape','bad')
            with self.assertRaises(ValueError):voice.safe_extract(archive,d/'extract')
            with zipfile.ZipFile(archive,'w') as z:z.writestr('model/am/file','good')
            voice.safe_extract(archive,d/'extract')
            self.assertEqual((d/'extract/model/am/file').read_text(),'good')

if __name__=='__main__':unittest.main()
