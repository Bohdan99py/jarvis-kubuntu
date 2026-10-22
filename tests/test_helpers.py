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
study = module('study')
speak = module('speak')

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

class Study(unittest.TestCase):
    def test_timestamps_and_segments(self):
        self.assertEqual(study.timestamp(83.9),'01:23')
        self.assertEqual(study.timestamp(3725),'1:02:05')
        self.assertEqual(study.clean_segment('  Bonjour   à tous. '),'Bonjour à tous.')
        self.assertEqual(study.clean_segment("Sous-titres réalisés par la communauté d'Amara.org"),'')
        self.assertEqual(study.clean_segment('...'),'')
        self.assertEqual(study.clean_segment(', '.join(["d'électronique et de programmation"]*12)),'','loops dropped')
        terms="Cours de systèmes embarqués, d'électronique et de programmation : microcontrôleur, STM32, registre, GPIO"
        self.assertEqual(study.clean_segment("Cours de systèmes embarqués, d'électronique et de programmation.",terms),'','echoed hotwords dropped')
        self.assertEqual(study.clean_segment("On configure le microcontrôleur STM32 et le GPIO.",terms),"On configure le microcontrôleur STM32 et le GPIO.")
    def test_chunker_cuts_at_pauses(self):
        import array, math
        def tone(seconds, amplitude):
            n=int(seconds*study.RATE)
            return array.array('h',(int(amplitude*32767*math.sin(i*0.3)) for i in range(n))).tobytes()
        def noise(seconds):
            n=int(seconds*study.RATE)
            return array.array('h',((i*7919)%61-30 for i in range(n))).tobytes()
        audio=noise(1)+tone(2,.3)+noise(1)+tone(1.5,.25)+noise(1.2)
        chunker=study.Chunker(shortest=1.2); phrases=[]
        for i in range(0,len(audio)-study.FRAME*2+1,study.FRAME*2):
            phrase=chunker.feed(audio[i:i+study.FRAME*2])
            if phrase: phrases.append(phrase)
        tail=chunker.flush()
        if tail: phrases.append(tail)
        self.assertEqual(len(phrases),2,[p[1] for p in phrases])
        self.assertAlmostEqual(phrases[0][1],1.0,delta=.1)
        self.assertAlmostEqual(phrases[1][1],4.0,delta=.1)
        self.assertGreater(len(phrases[0][0]),2*study.RATE*2*.95)
        self.assertLess(len(phrases[0][0]),3*study.RATE*2)
    def test_long_speech_is_split(self):
        import array, math
        audio=array.array('h',(int(.3*32767*math.sin(i*.3)) for i in range(20*study.RATE))).tobytes()
        chunker=study.Chunker(); cuts=0
        for i in range(0,len(audio),study.FRAME*2):
            cuts+=chunker.feed(audio[i:i+study.FRAME*2]) is not None
        self.assertEqual(cuts,1,'a 20 s monologue is cut at 15 s')
    def test_pdf_reflow(self):
        page=("Leçon 3 : Le passé composé\n"
              "Le passé composé est un temps du passé. On l'utilise pour parler d'une action\n"
              "terminée dans le passé. Il se forme avec l'auxiliaire avoir ou être au pré-\n"
              "sent, suivi du participe passé du verbe.\n"
              "- J'ai mangé une pomme.\n"
              "- Elle est allée au cinéma.\n"
              "12\n")
        self.assertEqual(study.reflow(page),[
            'Leçon 3 : Le passé composé',
            "Le passé composé est un temps du passé. On l'utilise pour parler d'une action terminée dans le passé. "
            "Il se forme avec l'auxiliaire avoir ou être au présent, suivi du participe passé du verbe.",
            "- J'ai mangé une pomme.", '- Elle est allée au cinéma.'])
        self.assertEqual(study.reflow(''),[])
        parts=study.split_long(['Phrase numéro un. '*200],limit=500)
        self.assertTrue(all(len(p)<=500 for p in parts) and len(parts)>5)
        self.assertTrue(all(p.endswith('.') for p in parts))
    def test_transcript_files(self):
        with tempfile.TemporaryDirectory() as d:
            audio=Path(d)/'cours.3.wav'
            study.write_transcript(audio,[{'start':0.0,'end':2.0,'text':'Bonjour.'},{'start':65,'end':70,'text':'Au revoir.'}],70,'small')
            data=json.loads((Path(d)/'cours.3.fr.json').read_text())
            self.assertEqual(data['segments'][1]['text'],'Au revoir.')
            self.assertEqual((Path(d)/'cours.3.fr.txt').read_text(),'[00:00] Bonjour.\n[01:05] Au revoir.\n')

class Speech(unittest.TestCase):
    def test_speakable_and_sentences(self):
        self.assertEqual(' '.join(speak.speakable('**Готово**: см. `main.c`\n```c\nint x;\n```', 'ru').split()), 'Готово: см. main.c Код показан в чате.')
        parts = speak.sentences('Первое предложение. Второе! ' + 'слово, ' * 80 + 'конец.', limit=120)
        self.assertTrue(all(len(p) <= 121 for p in parts) and len(parts) > 3)
        self.assertEqual(speak.sentences('One. Two.'), ['One. Two.'])
    def test_engine_plan(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            settings = speak.load_settings(root)
            self.assertEqual(settings['engine'], 'auto')
            self.assertEqual(speak.plan(settings, root), ['system'])
            (root / 'speech.json').write_text(json.dumps({'engine': 'elevenlabs', 'elevenlabs': {'api_key': 'k'}}))
            (root / 'piper').mkdir(); (root / 'piper' / 'ready').write_text('x')
            (root / 'venv/bin').mkdir(parents=True); (root / 'venv/bin/python').write_text('')
            (root / 'piper' / 'settings.json').write_text(json.dumps({'ru': 'ru_RU-irina-medium', 'rate': 9}))
            settings = speak.load_settings(root)
            self.assertEqual(speak.plan(settings, root), ['elevenlabs', 'piper', 'system'])
            self.assertEqual(settings['voices']['ru'], 'ru_RU-irina-medium')
            self.assertEqual(settings['rate'], 2.0)
            (root / 'speech.json').write_text(json.dumps({'engine': 'chatterbox'}))
            self.assertEqual(speak.plan(speak.load_settings(root), root), ['piper', 'system'], 'not installed: fallbacks')
    def test_elevenlabs_request(self):
        sent, played = {}, []
        class Response(io.BytesIO):
            def read1(self, n=-1): return self.read(min(n, 5))
        def urlopen(request, timeout):
            sent.update(url=request.full_url, body=json.loads(request.data), key=request.get_header('Xi-api-key'))
            return Response(b'\x01\x02\x03\x04\x05\x06\x07\x08\x09')
        def play(chunks, job, rate, channels=1):
            played.extend(chunks); return True
        settings = {'rate': 1.5, 'elevenlabs': {'api_key': 'secret', 'voice_id': 'onwK4e9ZLuTAKqWW03F9', 'model': 'eleven_flash_v2_5'}}
        with patch('urllib.request.urlopen', urlopen), patch.object(speak, 'play', play):
            self.assertTrue(speak.speak_elevenlabs('Привет', 'ru', settings, Path('/nonexistent'), speak.Job()))
        self.assertIn('/v1/text-to-speech/onwK4e9ZLuTAKqWW03F9/stream?output_format=pcm_24000', sent['url'])
        self.assertEqual(sent['key'], 'secret')
        self.assertEqual(sent['body']['language_code'], 'ru')
        self.assertEqual(sent['body']['voice_settings']['speed'], 1.2, 'ElevenLabs speed is limited to 0.7-1.2')
        self.assertTrue(all(len(c) % 2 == 0 for c in played), 'whole 16-bit samples only')
        settings['elevenlabs'] = {'api_key': 'secret', 'voice_id': '../../x'}
        with patch('urllib.request.urlopen', urlopen), patch.object(speak, 'play', play):
            speak.speak_elevenlabs('Hi', 'en', settings, Path('/nonexistent'), speak.Job())
        self.assertIn(speak.ELEVEN_DEFAULT_VOICE, sent['url'], 'an odd voice id never reaches the URL')
        self.assertNotIn('language_code', sent['body'])
    def test_server_round_trip(self):
        import os, subprocess, sys, time
        with tempfile.TemporaryDirectory() as d:
            d = Path(d); bin_dir = d / 'bin'; bin_dir.mkdir(); out = d / 'spoken.txt'
            tool = bin_dir / 'spd-say'
            tool.write_text(f'#!/bin/sh\ncat >> {out}\n'); tool.chmod(0o755)
            env = dict(os.environ, XDG_RUNTIME_DIR=str(d), XDG_DATA_HOME=str(d / 'data'), PATH=f'{bin_dir}:{os.environ["PATH"]}')
            script = str(ROOT / 'scripts' / 'speak.py')
            # -S: no site packages, so Speech Dispatcher's Python module is absent and spd-say speaks.
            server = subprocess.Popen([sys.executable, '-S', script, '--serve'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=env)
            try:
                self.assertIn(b'ready', server.stdout.readline())
                self.assertTrue((d / 'jarvis-speech.sock').exists())
                self.assertEqual((d / 'jarvis-speech.sock').stat().st_mode & 0o777, 0o600)
                client = subprocess.run([sys.executable, '-S', script, 'en'], input='**Hello** there.'.encode(), capture_output=True, env=env, timeout=30)
                self.assertEqual(client.returncode, 0, client.stderr)
                self.assertIn('"name": "system"', client.stdout.decode())
                self.assertEqual(out.read_text(), 'Hello there.')
            finally:
                server.stdin.close(); server.wait(timeout=10)
            self.assertFalse((d / 'jarvis-speech.sock').exists(), 'the socket goes with the server')

if __name__=='__main__':unittest.main()
