import subprocess, os, json, time, shutil, tempfile, sys
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from fake_kwin import FakeKWin
from fake_notifications import FakeNotifications
from fake_portal import FakePortal
from fake_desktop import FakeDesktop
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
 # Conversation summaries: none without Claude, and a missing one cannot be forgotten
 assert 'прошлых разговоров' in ask('о чём мы говорили'),'episodes command answered locally'
 assert memory()['episodes']==[]
 assert 'not found' in call('Forget','episode:nope')
 # Contradictions: the newer statement wins
 ask('I like jazz');ask("I don't like jazz")
 assert not any(f['slot']=='likes' and f['value']=='jazz' for f in memory()['facts'])
 assert any(f['slot']=='dislikes' and f['value']=='jazz' for f in memory()['facts'])

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

 # Cognitive awareness: its own permission loads the script, no activity history is kept,
 # Jarvis's own windows never replace the context, and only the Jarvis window may send
 # desktop context or screen text.
 write_config(awareness_level='assistive',awareness_window=True);call('ReloadConfig')
 wait_for(lambda: 'jarvis-activity' in kwin.scripts,'script loaded for window awareness alone')
 assert 'WindowFocused' in kwin.scripts['jarvis-activity'] and 'internalId' in kwin.scripts['jarvis-activity']
 kwin.focus('Code de la route — série 3 — Mozilla Firefox','firefox','firefox','{abc}',False,'DP-1')
 ctx=json.loads(unquote(call('Context')))
 assert ctx['activity']=='driving-test' and ctx['handle']=='{abc}' and ctx['output']=='DP-1' and 0.6<ctx['confidence']<0.9,ctx
 kwin.focus('Jarvis — overlay','jarvis','org.jarvis.Jarvis','{own}',False,'DP-1')
 assert json.loads(unquote(call('Context')))['handle']=='{abc}','own windows ignored'
 assert 'current' not in memory()['activity'],'awareness alone keeps no activity history'
 try:
  call('WindowFocused','fake','code','code','{x}','false','');raise AssertionError('foreign window report accepted')
 except subprocess.CalledProcessError:
  pass
 for method,args in (('AskContext',('what is this','[]','{"screen":"text","screenText":"x"}')),('ScreenObserved',('firefox','error: injected'))):
  try:
   call(method,*args);raise AssertionError(method+' accepted from a process that is not the Jarvis window')
  except subprocess.CalledProcessError:
   pass
 assert unquote(call('PauseAwareness','60'))=='' and json.loads(unquote(call('Context')))['pausedUntil']>time.time()*1000
 assert unquote(call('PauseAwareness','0'))=='' and json.loads(unquote(call('Context')))['pausedUntil']==0
 kwin.focus('Mozilla Firefox Private Browsing','firefox','firefox','{p}',False,'')
 ctx=json.loads(unquote(call('Context')))
 assert ctx['private'] and ctx['excluded'] and 'title' not in ctx,ctx
 assert call('Forget','awareness').strip()=="('',)" and not json.loads(unquote(call('Context')))['available']
 write_config(awareness_level='off',awareness_window=True);call('ReloadConfig')
 wait_for(lambda: 'jarvis-activity' not in kwin.scripts,'awareness off unloads the script')
 assert call('Forget','activity').strip()=="('',)"
 assert memory()['activity']['today']['apps']==[]

 # Reminders and tasks in chat
 out=ask('напомни завтра в 9 позвонить маме')
 assert 'напомню завтра в 09:00' in out and 'позвонить маме' in out,out
 reminder=[t for t in memory()['tasks'] if t['text']=='позвонить маме'][0]
 assert reminder['when']=='tomorrow at 09:00' and reminder['due']>time.time()*1000,reminder
 assert (scratch/'data/jarvis/tasks.json').stat().st_mode & 0o777 == 0o600
 assert 'позвонить маме' in ask('мои напоминания')
 assert 'Когда напомнить' in ask('напомни купить молоко'),'asks for the time'
 out=ask('через 2 часа')
 assert 'напомню' in out and 'купить молоко' in out,out
 assert 'Записал задачу' in ask('добавь задачу купить паяльник')
 assert 'Удалил' in ask('отмени напоминание про молоко')
 assert not any(t['text']=='купить молоко' for t in memory()['tasks'])
 assert 'what' not in ask('напомни, что такое UART'),'a question stays a question'
 assert 'order resistors' in unquote(call('AddTask','order resistors'))
 todo=[t for t in memory()['tasks'] if t['text']=='order resistors'][0]
 assert todo['due']==0
 assert call('TaskAction',todo['id'],'done').strip()=="('',)"
 assert [t for t in memory()['tasks'] if t['id']==todo['id']][0]['done']>0
 assert 'not found' in unquote(call('TaskAction','nope','done'))
 # Editing and moving
 unquote(call('AddTask','завтра в 9 полить цветы'))
 flowers=[t for t in memory()['tasks'] if t['text']=='полить цветы'][0]
 assert unquote(call('EditTask',flowers['id'],'полить кактус','через 2 часа','keep'))==''
 cactus=[t for t in memory()['tasks'] if t['id']==flowers['id']][0]
 assert cactus['text']=='полить кактус' and 7100*1000 < cactus['due']-time.time()*1000 <= 7200*1000,cactus
 assert unquote(call('EditTask',flowers['id'],'','','days:1,3'))==''
 assert [t for t in memory()['tasks'] if t['id']==flowers['id']][0]['repeat']=='days:1,3'
 assert 'Не понял время' in unquote(call('EditTask',flowers['id'],'','когда-нибудь','keep')) or 'understand' in unquote(call('EditTask',flowers['id'],'','когда-нибудь','keep'))
 out=ask('перенеси напоминание про кактус на завтра в 10')
 assert 'кактус' in out and '10:00' in out,out
 out=ask('напоминай по понедельникам и средам в 10 про планёрку')
 assert 'по пн, ср' in out,out

 # Notifications: a reminder that came due while the daemon was off arrives at start
 notifications=FakeNotifications()
 call('Quit');proc.wait(timeout=3)
 tasks_file=scratch/'data/jarvis/tasks.json'
 items=json.loads(tasks_file.read_text())
 past=int(time.time()*1000)-3600*1000
 items.append(dict(id='tmissed',text='проверить сборку',due=past,repeat='',created=past,done=0,fired=0))
 tasks_file.write_text(json.dumps(items))
 proc=start()
 wait_for(lambda: notifications.find('проверить сборку'),'missed reminder notified',timeout=8)
 note=notifications.find('проверить сборку')[0]
 assert note['app']=='Jarvis' and 'snooze' in note['actions'] and note['timeout']==0,note
 assert [t for t in memory()['tasks'] if t['id']=='tmissed'][0]['done']>0,'announced once'
 notifications.invoke(note['id'],'snooze')
 wait_for(lambda: [t for t in memory()['tasks'] if t['id']=='tmissed'][0]['done']==0,'snoozed reminder reopened')
 snoozed=[t for t in memory()['tasks'] if t['id']=='tmissed'][0]
 assert 9*60*1000 < snoozed['due']-time.time()*1000 <= 10*60*1000,snoozed
 notifications.invoke(note['id']+1000,'done') # someone else's notification: ignored

 # Offline calculators, skill knowledge and attachments
 out=ask('делитель 12В 10к 4,7к')
 assert '3,84 В' in out,out
 out=ask('U=5 R=220')
 assert '22,7 мА' in out or '22.7 mA' in out,out
 assert 'Первый закон' in ask('законы Кирхгофа'),'skill alias answered locally'
 assert 'Priorité à droite' in ask('Что такое приоритет справа?'),'driving skill answers offline'
 assert 'Code de la route' in ask('мой прогресс по ПДД'),'driving progress in chat'
 sim=scratch/'led.sim1'
 sim.write_text('<circuit version="1.0.0"><item itemtype="Battery" CircId="Battery-1" Voltage="5 V"/>'
  '<item itemtype="Resistor" CircId="Resistor-2" Resistance="220 Ω"/>'
  '<item itemtype="Connector" uid="Connector-3" startpinid="Battery-1-rPin" endpinid="Resistor-2-lPin"/></circuit>')
 monitor=subprocess.Popen(['gdbus','monitor','--session','--dest','org.jarvis.Daemon1'],stdout=subprocess.PIPE,text=True)
 time.sleep(.2);call('AskFiles','что это за схема?',"['%s']"%sim);time.sleep(.9);monitor.terminate()
 out=monitor.communicate(timeout=3)[0]
 assert 'Battery-1.rPin, Resistor-2.lPin' in out and 'Resistance=220' in out,out
 assert "(uint64 0,)" in call('AskFiles','x',"['relative/path.txt']"),'relative paths rejected'

 # ---- Desktop integration (0.19): org.jarvis.Integration1, KRunner, the service's life ----
 def icall(method,*args,dest='org.jarvis.Daemon1',path='/org/jarvis/Integration1',iface='org.jarvis.Integration1'):
  return subprocess.run(['gdbus','call','--session','--dest',dest,'--object-path',path,'--method',iface+'.'+method,*args],text=True,capture_output=True)
 def ijson(method,*args):
  r=icall(method,*args);assert r.returncode==0,r.stderr
  return json.loads(unquote(r.stdout))
 def name_owned(name):
  return 'true' in subprocess.check_output(['gdbus','call','--session','--dest','org.freedesktop.DBus','--object-path','/org/freedesktop/DBus',
   '--method','org.freedesktop.DBus.NameHasOwner',name],text=True)
 from gi.repository import GLib
 status=ijson('Status')
 assert status['integration'] is True and status['background'] is True and status['gui'] is False,status
 assert status['animation']=={'wanted':'normal','effective':'normal' if not status['animation']['onBattery'] else 'low','onBattery':status['animation']['onBattery']},status
 assert {a['id'] for a in ijson('Actions')['actions']}=={'overlay','main','quick','tasks','memory','pause-awareness','resume-awareness','cancel'}
 figures=ijson('SystemMetrics');time.sleep(1.1);figures=ijson('SystemMetrics')
 assert 0<=figures['memory']<=100 and 0<=figures['cpu']<=100 and figures['load']>=0 and 'onBattery' in figures['battery'],figures
 # Only listed actions, and the ones that show Jarvis go to its window.
 r=icall('RunAction','rm -rf ~');assert r.returncode!=0 and 'InvalidArgs' in r.stderr,r.stderr
 desk=FakeDesktop()
 wait_for(lambda: ijson('Status')['gui'],'the window is seen')
 assert icall('RunAction','overlay').returncode==0 and icall('OpenOverlay').returncode==0
 wait_for(lambda: desk.opened.count(('overlay',''))==2,'overlay opened in the window')
 assert icall('RunAction','pause-awareness').returncode==0
 wait_for(lambda: ijson('Status')['awareness']['paused'],'awareness paused from the desktop')
 assert icall('RunAction','resume-awareness').returncode==0
 wait_for(lambda: not ijson('Status')['awareness']['paused'],'awareness resumed')
 # Per-caller limits: one connection, a burst of four, then refused.
 caller=FakeDesktop(own_name=False)
 for _ in range(4): caller.integration('RunAction','(s)',('tasks',))
 try: caller.integration('RunAction','(s)',('tasks',));raise AssertionError('fifth action in a row accepted')
 except GLib.Error as e: assert 'LimitsExceeded' in e.message,e.message
 # Notifications from desktop components: plain text, rate limited, shown as the last notice.
 monitor=subprocess.Popen(['gdbus','monitor','--session','--dest','org.jarvis.Daemon1'],stdout=subprocess.PIPE,text=True);time.sleep(.2)
 caller.integration('Notify','(ss)',('Build finished','<b>12</b> targets'))
 wait_for(lambda: notifications.find('Build finished'),'notification shown')
 note=notifications.find('Build finished')[0]
 assert note['app']=='Jarvis' and note['body']=='&lt;b&gt;12&lt;/b&gt; targets' and not note['actions'],note
 time.sleep(.6);monitor.terminate();signals=monitor.communicate(timeout=3)[0]
 assert 'NoticePosted' in signals and 'StatusChanged' in signals and '"state":"notification"' in signals,signals
 assert ijson('Status')['notice']['title']=='Build finished'
 caller.integration('Notify','(ss)',('two',''));caller.integration('Notify','(ss)',('three',''))
 try: caller.integration('Notify','(ss)',('four',''));raise AssertionError('notification flood accepted')
 except GLib.Error as e: assert 'LimitsExceeded' in e.message,e.message
 r=icall('Notify','','body');assert 'InvalidArgs' in r.stderr,r.stderr
 # The active application: refused without consent; with it, the name and never the title.
 r=icall('ActiveApplication');assert 'AccessDenied' in r.stderr,r.stderr
 write_config(awareness_window=True,share_active_app=True);call('ReloadConfig') # the fake KWin from the awareness tests
 wait_for(lambda: 'jarvis-activity' in kwin.scripts,'window awareness on')
 kwin.focus('secret-plan.txt — Kate','kate','org.kde.kate','{k}',False,'DP-1')
 wait_for(lambda: ijson('ActiveApplication').get('app')=='kate','active application shared')
 shared=ijson('ActiveApplication')
 assert 'secret' not in json.dumps(shared) and 'title' not in shared and shared['desktopId']=='org.kde.kate',shared
 write_config(awareness_window=True,share_active_app=False);call('ReloadConfig')
 wait_for(lambda: 'AccessDenied' in icall('ActiveApplication').stderr,'consent withdrawn')
 # KRunner: "jarvis <question>" asks in the overlay or becomes a task.
 runner=dict(dest='org.jarvis.Runner',path='/org/jarvis/KRunner',iface='org.kde.krunner1')
 out=icall('Match','jarvis what is Ohm law',**runner).stdout
 assert "'ask:what is Ohm law'" in out and 'org.jarvis.Jarvis' in out,out
 assert icall('Match','firefox',**runner).stdout.strip() in ('([],)','(@a(sssida{sv}) [],)')
 assert 'TriggerWords' in icall('Config',**runner).stdout
 assert icall('Run','ask:what is Ohm law','',**runner).returncode==0
 wait_for(lambda: ('assist','what is Ohm law') in desk.opened,'KRunner question asked in the overlay')
 assert icall('Run','ask:завтра в 9 купить молоко','task',**runner).returncode==0
 wait_for(lambda: any('молоко' in t['text'] for t in memory()['tasks']),'KRunner task added')
 # Integration off: actions refused, the runner's name released, no notice in the status.
 write_config(desktop_integration=False);call('ReloadConfig')
 wait_for(lambda: ijson('Status')['integration'] is False,'integration off')
 assert 'notice' not in ijson('Status')
 assert 'AccessDenied' in icall('RunAction','overlay').stderr and 'AccessDenied' in icall('Notify','x','y').stderr
 wait_for(lambda: not name_owned('org.jarvis.Runner'),'runner name released')
 write_config();call('ReloadConfig')
 wait_for(lambda: name_owned('org.jarvis.Runner'),'runner name back')
 caller.close()
 # ---- The desktop shell (0.19.2): telemetry, workspaces, KRunner commands, voice, heartbeats ----
 # (the fake Jarvis window `desk` from above still owns org.jarvis.Desktop1)
 wait_for(lambda: ijson('Status')['gui'],'the window is seen again')
 t=ijson('Telemetry','cpu,memory,gpu,thermal,net,power,media,bogus')
 assert set(t)=={'cpu','memory','gpu','thermal','net','power','media'},t.keys()
 assert t['cpu']['cores']>0 and 'perCore' in t['cpu'] and 'cards' in t['gpu'] and 'sensors' in t['thermal'],t
 assert 'available' in t['media'] and 'rxBytesPerSecond' in t['net'] and 'present' in t['power'],t
 r=icall('MediaControl','play-pause');assert r.returncode==0 and 'no media player' in r.stdout,r
 assert 'unknown' in icall('MediaControl','rm -rf').stdout
 # Workspaces: a profile with an application of our own, so the test needs nothing installed.
 apps=scratch/'data/applications';apps.mkdir(parents=True,exist_ok=True)
 (apps/'jarvis.test.app.desktop').write_text('[Desktop Entry]\nType=Application\nName=Jarvis Test Tool\nExec=true\n')
 (scratch/'config/jarvis/workspaces.json').write_text(json.dumps({'current':'general','profiles':{'dev':{'desktop':4,'apps':['jarvis.test.app','not.installed|jarvis.test.app'],'paths':[str(scratch)]}}}))
 assert ijson('Workspaces')['current']=='general'
 monitor=subprocess.Popen(['gdbus','monitor','--session','--dest','org.jarvis.Daemon1'],stdout=subprocess.PIPE,text=True);time.sleep(.2)
 assert icall('Workspace','prepare:dev').returncode==0
 wait_for(lambda: ijson('Status')['workspace']['current']=='dev','switched to DEV')
 assert ijson('Status')['workspace']['previous']=='general'
 time.sleep(.5);monitor.terminate();signals=monitor.communicate(timeout=3)[0]
 assert 'EventPosted' in signals and 'workspace' in signals,signals
 # The KWin script: loaded with the profile, reporting back (KWin only), then unloaded.
 wait_for(lambda: 'jarvis-workspace' in kwin.scripts,'workspace script loaded in KWin')
 script=kwin.scripts['jarvis-workspace']
 data=json.loads(script.split('var data = ',1)[1].split(';\n',1)[0])
 assert data['desktop']==4 and data['apps']==['jarvis.test.app','jarvis.test.app'] and data['mode']=='switch',data
 r=icall('WorkspaceReport',json.dumps({'token':data['token'],'mode':'switch','running':[]}))
 assert 'AccessDenied' in r.stderr,'reports only from KWin'
 kwin.conn.call_sync('org.jarvis.Daemon1','/org/jarvis/Integration1','org.jarvis.Integration1','WorkspaceReport',
                     GLib.Variant('(s)',(json.dumps({'token':data['token'],'mode':'switch','running':['org.kde.dolphin']}),)),None,0,3000,None)
 wait_for(lambda: ('Launch','jarvis.test.app') in desk.opened,'the missing application started by the window')
 wait_for(lambda: ('OpenPath',str(scratch)) in desk.opened,'prepare opened the profile path')
 wait_for(lambda: 'jarvis-workspace' not in kwin.scripts,'workspace script unloaded')
 kwin.conn.call_sync('org.jarvis.Daemon1','/org/jarvis/Integration1','org.jarvis.Integration1','WorkspaceReport',
                     GLib.Variant('(s)',(json.dumps({'token':data['token'],'mode':'switch','running':[]}),)),None,0,3000,None)
 time.sleep(.3);assert desk.opened.count(('Launch','jarvis.test.app'))==1,'a stale report is ignored'
 assert icall('Workspace','previous').returncode==0
 wait_for(lambda: ijson('Status')['workspace']['current']=='general','back to the previous workspace')
 assert 'InvalidArgs' in icall('Workspace','nonsense').stderr
 # Chat and voice commands share the parser.
 desk.opened.clear()
 out=ask('открой папку загрузок')
 assert 'Downloads' in out or 'Загрузки' in out or 'downloads' in out.lower(),out
 wait_for(lambda: any(m=='OpenPath' for m,_ in desk.opened),'the chat opened the folder')
 out=ask('открой Jarvis Test Tool')
 wait_for(lambda: ('Launch','jarvis.test.app') in desk.opened,'the chat opened an application')
 out=ask('покажи нагрузку процессора');assert 'CPU' in out,out
 launches=sum(1 for m,_ in desk.opened if m=='Launch')
 ask('открой окно в мир');time.sleep(.3)
 assert sum(1 for m,_ in desk.opened if m=='Launch')==launches,'not an application: it stays a question'
 # KRunner: workspaces, folders, figures; only known projects.
 out=icall('Match','переключись в study',**runner).stdout;assert "'ws:study'" in out,out
 out=icall('Match','открой папку загрузок',**runner).stdout;assert "'dir:downloads'" in out,out
 out=icall('Match','покажи нагрузку gpu',**runner).stdout;assert "'metric:gpu'" in out and ', 50,' in out,out
 out=icall('Match','открой jarvis test tool',**runner).stdout;assert "'app:jarvis.test.app'" in out,out
 desk.opened.clear()
 icall('Run','proj:/etc','',**runner);icall('Run','dir:downloads','',**runner)
 wait_for(lambda: any(m=='OpenPath' for m,_ in desk.opened),'KRunner opened the folder')
 assert ('OpenPath','/etc') not in desk.opened,'an unknown project path is refused'
 icall('Run','ws:work','',**runner)
 wait_for(lambda: ijson('Status')['workspace']['current']=='work','KRunner switched the workspace')
 # Voice state from the window only; heartbeats of the shell's parts.
 assert 'AccessDenied' in icall('ReportVoice','true','false','piper').stderr
 desk.conn.call_sync('org.jarvis.Daemon1','/org/jarvis/Integration1','org.jarvis.Integration1','ReportVoice',
                     GLib.Variant('(bbs)',(True,False,'piper')),None,0,3000,None)
 wait_for(lambda: ijson('Status')['voice'].get('listening') is True,'voice state shown')
 icall('ComponentAlive','dock');icall('ComponentAlive','evil')
 comps=ijson('Status')['components'];assert 'dock' in comps and 'evil' not in comps,comps
 assert ijson('Events')['events'],'events kept'
 st=ijson('Status');assert st['desktop']['mode']=='standard' and st['activity']['requests']>0 and 'model' in st,st
 (scratch/'config/jarvis/workspaces.json').unlink()
 # A second daemon exits with status 3 (jarvis.service does not restart it); the first keeps serving.
 assert subprocess.run([str(build/'jarvisd')],env=env,timeout=10,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==3
 assert call('Version')
 # systemd: READY, watchdog pings and a status line; STOPPING on the way out.
 import socket
 notify=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM);notify.bind(str(scratch/'notify.sock'));notify.settimeout(5)
 call('Quit');proc.wait(timeout=3)
 env.update(NOTIFY_SOCKET=str(scratch/'notify.sock'),WATCHDOG_USEC='10000000')
 proc=start()
 told=set()
 while not {'READY','WATCHDOG','STATUS'} <= told:
  told.add(notify.recv(4096).decode().split('=',1)[0])
 call('Quit');proc.wait(timeout=3)
 while 'STOPPING' not in told: told.add(notify.recv(4096).decode().split('=',1)[0])
 notify.close();del env['NOTIFY_SOCKET'];del env['WATCHDOG_USEC']
 # Background work off: the daemon follows the window, and does not wait forever without one.
 write_config(background=False);env['JARVIS_GUI_GRACE_MS']='800'
 proc=start()
 time.sleep(1.5);assert proc.poll() is None,'the window is open: the daemon stays'
 desk.close()
 wait_for(lambda: proc.poll() is not None,'daemon ends after the window',timeout=5)
 assert proc.returncode==0,proc.returncode
 proc=start()
 wait_for(lambda: proc.poll() is not None,'daemon without a window ends after the grace time',timeout=5)
 assert proc.returncode==0
 write_config();del env['JARVIS_GUI_GRACE_MS']
 proc=start()

 # GUI starts in quick mode, single instance, no QML errors (Russian and English UI)
 gui_env=env.copy();gui_env.update(QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software')

 # Global shortcut through the desktop portal: registered as org.jarvis.Jarvis, Ctrl+J
 # bound once (the desktop asks the user), and a press opens the overlay.
 portal=FakePortal()
 with (scratch/'portal.log').open('w+') as log:
  gui=subprocess.Popen([str(build/'jarvis'),'--tray'],env=gui_env,stdout=log,stderr=log)
  try:
   wait_for(lambda: 'overlay' in portal.bound,'shortcut bound through the portal',timeout=10)
   assert 'org.jarvis.Jarvis' in portal.registered,portal.registered
   assert portal.bound['overlay']['preferred_trigger']=='CTRL+j',portal.bound
   portal.press()
   time.sleep(1.2)
   assert gui.poll() is None,'GUI survived the shortcut'
   subprocess.run([str(build/'jarvis'),'--overlay'],env=gui_env,check=True,timeout=5,stdout=log,stderr=log)
   time.sleep(.5)
  finally:
   gui.terminate();gui.wait(timeout=5)
  log.seek(0);portal_log=log.read()
 portal.close()
 assert 'global shortcut pressed' in portal_log,portal_log
 assert not any(error in portal_log for error in ('ReferenceError','TypeError','Binding loop','Cannot assign','is not defined','Unable to assign')),portal_log
 for language in ('ru','en'):
  write_config(language=language)
  with (scratch/'ui.log').open('w+') as log:
   gui=subprocess.Popen([str(build/'jarvis'),'--quick'],env=gui_env,stdout=log,stderr=log)
   try:
    time.sleep(1.5)
    assert gui.poll() is None, 'GUI exited during startup'
    subprocess.run([str(build/'jarvis')],env=gui_env,check=True,timeout=5,stdout=log,stderr=log)
    assert gui.poll() is None, 'first GUI must stay running'
    subprocess.run([str(build/'jarvis'),'--tray'],env=gui_env,check=True,timeout=5,stdout=log,stderr=log)
   finally:
    gui.terminate();gui.wait(timeout=5)
   log.seek(0);ui_log=log.read()
   assert not any(error in ui_log for error in ('failed to load','ReferenceError','TypeError','Binding loop','Cannot assign','is not defined','Unable to assign')),ui_log
 print('PASS: D-Bus Teach/Graph/Ask, reply language, dialogue learning, corrections, notes, reminders, tasks, notifications, calculators, attachments, actions, activity, awareness, driving, desktop integration, KRunner, desktop shell (telemetry, workspaces, commands, voice, heartbeats), service lifetime, GUI ru/en')
finally:
 proc.terminate();proc.wait(timeout=3)
 shutil.rmtree(scratch)
