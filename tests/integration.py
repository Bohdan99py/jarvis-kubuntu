import subprocess, os, json, time, shutil, tempfile, sys
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from fake_kwin import FakeKWin
from pathlib import Path
base=Path.cwd()
build=Path(os.environ.get('JARVIS_TEST_BUILD', str(base/'build')))
scratch=Path(tempfile.mkdtemp(prefix='jarvis-test-'))
env=os.environ.copy();env['XDG_DATA_HOME']=str(scratch/'data');env['XDG_CONFIG_HOME']=str(scratch/'config');env['ANTHROPIC_API_KEY']=''
env['XDG_RUNTIME_DIR']=str(scratch/'run');(scratch/'run').mkdir(mode=0o700)
config=scratch/'config/jarvis/config.json'
def write_config(**values):
 config.parent.mkdir(parents=True,exist_ok=True)
 config.write_text(json.dumps(dict(api_key='',model='claude-haiku-4-5-20251001',language='en',reply_language='auto',
  learn_dialog=True,track_activity=False,track_titles=False,share_activity=False)|values))
def call(method,*args):
 return subprocess.check_output(['gdbus','call','--session','--dest','org.jarvis.Daemon1','--object-path','/org/jarvis/Daemon1','--method','org.jarvis.Daemon1.'+method,*args],text=True)
def unquote(output):
 # gdbus prints a GVariant tuple such as ('…',) or ("…",); close enough to a Python literal.
 import ast
 return ast.literal_eval(output.strip())[0]
def memory():
 return json.loads(unquote(call('Memory')))
def ask(text,wait=.9):
 monitor=subprocess.Popen(['gdbus','monitor','--session','--dest','org.jarvis.Daemon1'],stdout=subprocess.PIPE,text=True)
 time.sleep(.2);call('Ask',text);time.sleep(wait);monitor.terminate()
 return monitor.communicate(timeout=3)[0]
def wait_for(check,message,timeout=5):
 deadline=time.time()+timeout
 while time.time()<deadline:
  if check(): return
  time.sleep(.1)
 raise AssertionError(message)
class Ide:
 """The VS Code extension's side of the IDE socket."""
 def __init__(self):
  import socket
  self.sock=socket.socket(socket.AF_UNIX);self.sock.settimeout(5);self.sock.connect(str(scratch/'run/jarvis-ide.sock'))
  self.buffer=b'';self.next=1
 def send(self,message):
  self.sock.sendall((json.dumps(message)+'\n').encode())
 def request(self,message):
  message=dict(message,id=self.next);self.next+=1;self.send(message)
  while True:
   while b'\n' not in self.buffer: self.buffer+=self.sock.recv(65536)
   line,self.buffer=self.buffer.split(b'\n',1)
   reply=json.loads(line)
   if reply.get('id')==message['id']: return reply
 def close(self): self.sock.close()
def start():
 proc=subprocess.Popen([str(build/'jarvisd')],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 time.sleep(.15)
 assert proc.poll() is None
 for _ in range(50):
  try: call('Version');return proc
  except subprocess.CalledProcessError: time.sleep(.05)
 raise RuntimeError('daemon unavailable')
write_config()
proc=start()
try:
 # Taught examples
 assert call('Teach','Мой редактор','Используй Kate').strip()=="('',)"
 assert 'редактор' in call('Graph')
 assert 'question and an answer' in call('Teach','',''), 'English UI language for daemon messages'
 assert call('Teach','Мой редактор','Используй Vim').strip()=="('',)"
 items=json.loads((scratch/'data/jarvis/learning.json').read_text())
 assert len(items)==1 and items[0]['answer']=='Используй Vim'
 assert (scratch/'data/jarvis/learning.json').stat().st_mode & 0o777 == 0o600
 call('Quit');proc.wait(timeout=3);proc=start()
 assert 'Используй Vim' in ask('МОЙ РЕДАКТОР?!')
 assert '"examples":1' in call('Graph')

 # Reply language follows the message
 out=ask('hello')
 assert any(w in out for w in ('Hello!','Hi there!','Greetings!')),out
 out=ask('привет')
 assert any(w in out for w in ('Привет!','Здравствуй!','Приветствую!','Рад тебя слышать!')),out
 # …or is forced
 write_config(reply_language='en');call('ReloadConfig')
 out=ask('привет')
 assert any(w in out for w in ('Hello!','Hi there!','Greetings!')),out
 write_config();call('ReloadConfig')

 # Learning from the dialogue
 ask('Меня зовут Богдан, я работаю программистом.')
 facts=memory()['facts']
 assert any(f['slot']=='name' and f['value']=='Богдан' for f in facts),facts
 assert (scratch/'data/jarvis/knowledge.json').stat().st_mode & 0o777 == 0o600
 assert 'Богдан' in ask('что ты обо мне знаешь'),'facts recalled'
 assert 'Kate' in ask('запомни, что я предпочитаю Kate')
 assert any(f['source']=='manual' and 'Kate' in f['value'] for f in memory()['facts'])
 assert 'пароль' in ask('запомни мой пароль hunter2')
 assert not any('hunter2' in f['value'] for f in memory()['facts']),'secrets never stored'
 assert 'Забыл' not in ask('forget it'),'pronouns forget nothing'
 assert 'Забыл' in ask('забудь Kate')
 assert not any('Kate' in f['value'] for f in memory()['facts'])
 note=call('Remember','likes dark themes');assert note.strip()=="('',)",note
 fact_id=[f['id'] for f in memory()['facts'] if f['value']=='likes dark themes'][0]
 assert call('Forget',fact_id).strip()=="('',)"
 assert not any(f['id']==fact_id for f in memory()['facts'])
 assert memory()['topics'],'topics learned from messages'

 # Curiosity: after a couple of messages Jarvis asks, then remembers the answer
 assert call('Forget','facts').strip()=="('',)"
 ask('привет')
 out=ask('как дела')
 assert 'обращаться' in out,out
 out=ask('Алекс')
 assert 'Приятно познакомиться, Алекс' in out,out
 assert any(f['slot']=='name' and f['value']=='Алекс' and f['source']=='curiosity' for f in memory()['facts'])
 assert unquote(call('Curious')),'another question is ready'
 assert memory()['curiosity'].get('pending'),'question waiting for an answer'

 # Programming: the VS Code extension's socket
 sock=scratch/'run/jarvis-ide.sock'
 assert sock.exists() and sock.stat().st_mode & 0o077 == 0,'IDE socket is owner-only'
 ide=Ide()
 hello=ide.request({'type':'hello','client':'test'})
 assert hello['type']=='hello' and hello['claude'] is False,hello
 wait_for(lambda: memory()['code']['connected']==1,'IDE client counted')
 ide.send({'type':'workspace','project':'demo','languages':['cpp'],'frameworks':['Qt 6','CMake']})
 ide.send({'type':'activity','language':'cpp','project':'demo'})
 ide.send({'type':'diagnostic','language':'cpp','project':'demo','message':"'foo' was not declared in this scope"})
 wait_for(lambda: memory()['code']['projects'] and memory()['code']['errors'],'project and error recorded')
 assert memory()['code']['projects'][0]['frameworks']==['Qt 6','CMake']
 assert any(f['value']=='Qt 6' and f['source']=='vscode' for f in memory()['facts']),'frameworks become facts'
 taught=ide.request({'type':'teach','language':'cpp','problem':"'foo' was not declared in this scope",'solution':'Include the header that declares it.'})
 assert taught['ok'],taught
 assert not ide.request({'type':'teach','language':'cpp','problem':'deploy','solution':'token ghp_abcdefghijklmnop'})['ok'],'secrets refused'
 reply=ide.request({'type':'ask','mode':'error','text':'','context':{'language':'cpp','file':'main.cpp','project':'demo','selection':'bar();',
  'diagnostics':[{'line':3,'severity':'error','message':"'bar' was not declared in this scope"}]}})
 assert reply['ok'] and 'Include the header' in reply['text'],reply
 rated=ide.request({'type':'rate','request':reply['request'],'good':True})
 assert rated['ok'] and rated['text'],rated
 assert not ide.request({'type':'ask','mode':'fix','text':'','context':{}})['ok'],'empty request rejected'
 assert not ide.request({'type':'ask','mode':'rm -rf','text':'x','context':{}})['ok'],'unknown mode rejected'
 assert memory()['code']['lessonCount']==1
 # The real extension code, with a stub of the VS Code API.
 node=os.environ.get('JARVIS_NODE') or shutil.which('node')
 if node:
  node_env=os.environ.copy();node_env['JARVIS_SOCKET']=str(sock)
  if 'ELECTRON' in node or node.endswith('/code'): node_env['ELECTRON_RUN_AS_NODE']='1'
  out=subprocess.run([node,str(Path(__file__).resolve().parent/'vscode_client_test.js')],env=node_env,capture_output=True,text=True,timeout=60)
  assert out.returncode==0,out.stdout+out.stderr
  print(out.stdout.strip())
 ide.close()
 wait_for(lambda: memory()['code']['connected']==0,'IDE client gone')

 # 👍/👎 in chat
 ask('Как зовут моего кота?')
 assert 'правильно' in unquote(call('Feedback','false'))
 assert 'Спасибо' in ask('Барсик')
 assert 'Барсик' in ask('Как зовут моего кота?')
 assert unquote(call('Feedback','true'))
 assert 'Алекс' in ask('что ты узнал сегодня')

 # Corrections teach the previous question
 ask('Какой мой любимый цвет?')
 assert 'исправил' in ask('нет, правильно: синий')
 assert 'синий' in ask('Какой мой любимый цвет?')
 ask('What is my favourite drink?')
 ask('remember this answer')
 assert any(i['question']=='What is my favourite drink?' for i in json.loads((scratch/'data/jarvis/learning.json').read_text()))

 # Actions and activity
 call('RecordAction','launch:terminal','Terminal');call('RecordAction','launch:terminal','Terminal')
 assert 'launch:terminal' in [s['id'] for s in memory()['activity']['suggestions']]
 assert 'off' in ask('what am I doing')
 # Tracking on before KWin exists (login race): the daemon waits for KWin.
 write_config(track_activity=True,track_titles=True);call('ReloadConfig')
 assert memory()['settings']['status']=='no-kwin','no KWin on the test bus yet'
 kwin=FakeKWin()
 wait_for(lambda: memory()['settings']['status']=='kwin','script loaded when KWin appears')
 script=kwin.scripts['jarvis-activity']
 assert '"org.jarvis.Daemon1"' in script and '%SERVICE%' not in script,'service name substituted'
 assert kwin.starts==1
 # Only KWin may report windows.
 try:
  call('WindowActivated','fake','code','code');raise AssertionError('foreign window report accepted')
 except subprocess.CalledProcessError:
  pass
 assert 'current' not in memory()['activity']
 kwin.report('main.cpp — jarvis','code','code')
 current=memory()['activity']['current']
 assert current['category']=='coding' and current['title']=='main.cpp — jarvis',current
 assert 'coding' in ask('what am I doing')
 kwin.report('Private Browsing — Mozilla Firefox','firefox','firefox')
 assert 'title' not in memory()['activity']['current'],'private titles dropped'
 kwin.report('','plasmashell','org.kde.plasmashell')
 assert 'current' not in memory()['activity']
 # KWin restarts: the script is loaded again.
 kwin.close()
 wait_for(lambda: memory()['settings']['status']=='no-kwin','KWin gone')
 kwin=FakeKWin()
 wait_for(lambda: 'jarvis-activity' in kwin.scripts,'script reloaded after KWin restart')
 # Turning tracking off unloads the script.
 write_config(track_activity=False);call('ReloadConfig')
 wait_for(lambda: 'jarvis-activity' not in kwin.scripts,'script unloaded')
 assert call('Forget','activity').strip()=="('',)"
 assert memory()['activity']['today']['apps']==[]

 # GUI starts in quick mode, single instance, no QML errors (Russian and English UI)
 gui_env=env.copy();gui_env.update(QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software')
 for language in ('ru','en'):
  write_config(language=language)
  with (scratch/'ui.log').open('w+') as log:
   gui=subprocess.Popen([str(build/'jarvis'),'--quick'],env=gui_env,stdout=log,stderr=log)
   try:
    time.sleep(1.5)
    assert gui.poll() is None, 'GUI exited during startup'
    subprocess.run([str(build/'jarvis')],env=gui_env,check=True,timeout=5,stdout=log,stderr=log)
    assert gui.poll() is None, 'first GUI must stay running'
   finally:
    gui.terminate();gui.wait(timeout=5)
   log.seek(0);ui_log=log.read()
   assert not any(error in ui_log for error in ('failed to load','ReferenceError','TypeError','Binding loop','Cannot assign','is not defined','Unable to assign')),ui_log
 print('PASS: D-Bus Teach/Graph/Ask, reply language, dialogue learning, corrections, notes, actions, activity, GUI ru/en')
finally:
 proc.terminate();proc.wait(timeout=3)
 shutil.rmtree(scratch)
