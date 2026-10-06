import subprocess, os, json, time
from pathlib import Path
base=Path.cwd()
build=Path(os.environ.get('JARVIS_TEST_BUILD', str(base/'build')))
import tempfile
scratch=Path(tempfile.mkdtemp(prefix='jarvis-test-'))
env=os.environ.copy();env['XDG_DATA_HOME']=str(scratch/'data');env['XDG_CONFIG_HOME']=str(scratch/'config');env['ANTHROPIC_API_KEY']=''
def call(method,*args):
 return subprocess.check_output(['gdbus','call','--session','--dest','org.jarvis.Daemon1','--object-path','/org/jarvis/Daemon1','--method','org.jarvis.Daemon1.'+method,*args],text=True)
def start():
 proc=subprocess.Popen([str(build/'jarvisd')],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 time.sleep(.15)
 assert proc.poll() is None
 for _ in range(50):
  try: call('Version');return proc
  except subprocess.CalledProcessError: time.sleep(.05)
 raise RuntimeError('daemon unavailable')
proc=start()
try:
 assert call('Teach','Мой редактор','Используй Kate').strip()=="('',)"
 assert 'редактор' in call('Graph')
 assert 'Нужны' in call('Teach','','')
 assert call('Teach','Мой редактор','Используй Vim').strip()=="('',)"
 items=json.loads((scratch/'data/jarvis/learning.json').read_text())
 assert len(items)==1 and items[0]['answer']=='Используй Vim'
 assert (scratch/'data/jarvis/learning.json').stat().st_mode & 0o777 == 0o600
 call('Quit');proc.wait(timeout=3);proc=start()
 monitor=subprocess.Popen(['gdbus','monitor','--session','--dest','org.jarvis.Daemon1'],stdout=subprocess.PIPE,text=True)
 time.sleep(.2);call('Ask','МОЙ РЕДАКТОР?!');time.sleep(.5);monitor.terminate()
 output=monitor.communicate(timeout=3)[0]
 assert 'Используй Vim' in output,output
 assert '"examples":1' in call('Graph')
 gui_env=env.copy();gui_env.update(QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software')
 with (scratch/'ui.log').open('w+') as log:
  gui=subprocess.Popen([str(build/'jarvis'),'--quick'],env=gui_env,stdout=log,stderr=log)
  try:
   time.sleep(1)
   assert gui.poll() is None, 'GUI exited during startup'
   subprocess.run([str(build/'jarvis')],env=gui_env,check=True,timeout=5,stdout=log,stderr=log)
   assert gui.poll() is None, 'first GUI must stay running'
  finally:
   gui.terminate();gui.wait(timeout=5)
  log.seek(0);ui_log=log.read()
  assert not any(error in ui_log for error in ('failed to load','ReferenceError','TypeError','Binding loop','Cannot assign')),ui_log
 print('PASS: D-Bus Teach/Graph/Ask, persistence, normalized recall, GUI quick mode and single instance')
finally:
 proc.terminate();proc.wait(timeout=3)
 import shutil
 shutil.rmtree(scratch)
