#pragma once
// Pagina del dashboard, guardada en el programa del UNO. Origen: dashboard.html

static const char AERIS_PAGE[] = R"PAGE(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AERIS</title>
<style>
:root{--bg:#f7f8fa;--card:#fff;--text:#17202a;--muted:#75808d;--line:#e5e9ed;--accent:#1d5c4f;--soft:#e9f2ef;--bad:#b94d43}
:root[data-theme="dark"]{--bg:#101418;--card:#171c21;--text:#edf2f5;--muted:#929ca7;--line:#283139;--accent:#71b6a6;--soft:#18312b;color-scheme:dark}
*{box-sizing:border-box}body{margin:0;font:14px/1.45 system-ui,sans-serif;background:var(--bg);color:var(--text)}
header{position:sticky;top:0;z-index:2;display:flex;gap:10px;align-items:center;justify-content:space-between;padding:12px 16px;background:color-mix(in srgb,var(--card) 92%,transparent);border-bottom:1px solid var(--line);backdrop-filter:blur(8px)}
.brand b{letter-spacing:.14em}.brand small{display:block;color:var(--muted);font-size:11px}
nav{display:flex;gap:6px;flex-wrap:wrap;align-items:center}
button,a.b{border:1px solid var(--line);background:var(--card);color:var(--text);border-radius:9px;padding:8px 10px;font:inherit;cursor:pointer;text-decoration:none}
button.on{background:var(--soft);color:var(--accent);border-color:transparent;font-weight:700}
#med{background:var(--text);color:var(--card);font-weight:700}
main{padding:16px;max-width:1100px;margin:auto}
h1{font-size:22px;margin:0}.sub{color:var(--muted);margin:4px 0 14px}
.kpis{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.kpi,.card{background:var(--card);border:1px solid var(--line);border-radius:14px}
.kpi{padding:12px}.kpi span{color:var(--muted);font-size:11px;font-weight:700}.kpi b{display:block;font-size:22px;margin-top:6px}.kpi small{font-size:12px;color:var(--muted);font-weight:600}
.grid{display:grid;grid-template-columns:1.4fr .8fr;gap:10px;margin-top:10px}
.card{padding:14px}.card h2{font-size:15px;margin:0 0 8px}
canvas{width:100%;height:180px;display:block}
.ring{width:140px;height:140px;border-radius:50%;margin:10px auto;display:grid;place-items:center;background:conic-gradient(var(--c) calc(var(--p)*1%),var(--line) 0)}
.ring i{width:108px;height:108px;border-radius:50%;background:var(--card);display:grid;place-items:center;font-style:normal;font-size:32px;font-weight:800}
table{width:100%;border-collapse:collapse;font-size:12px}th,td{text-align:left;padding:8px;border-bottom:1px solid var(--line);white-space:nowrap}th{color:var(--muted);font-size:10px;letter-spacing:.04em}
.alert{padding:8px 0;border-top:1px solid var(--line)}.err{background:#fff2ef;color:#9f433a;padding:12px;border-radius:12px}
.scroll{overflow:auto}
@media(max-width:800px){.kpis{grid-template-columns:1fr 1fr}.grid{grid-template-columns:1fr}header{flex-wrap:wrap}}
#tools{display:flex;flex-wrap:wrap;gap:8px;align-items:end;margin-top:10px}#tools select{padding:8px;border-radius:9px;border:1px solid var(--line);background:var(--card);color:var(--text)}
#alarm.arm,#alarm2.arm{background:var(--bad);color:#fff;border-color:transparent;font-weight:700}
</style>
</head>
<body>
<header>
  <div class="brand"><b>AERIS</b><small id="where">Datos en el Arduino</small></div>
  <nav>
    <button class="on" data-p="home" type="button">Panel</button>
    <button data-p="live" type="button">Tiempo real</button>
    <button data-p="hist" type="button">Historico</button>
    <button data-p="dev" type="button">Dispositivo</button>
    <a class="b" href="/wifi">WiFi</a>
    <button id="theme" type="button">Tema</button>
  </nav>
  <button id="med" type="button">Tomar medicion</button>
</header>
<main>
  <h1>Condiciones ambientales</h1>
  <p class="sub" id="sub">Lecturas guardadas en la memoria del Arduino.</p>
  <div id="err" class="err" hidden></div>
  <section id="home">
    <div class="kpis" id="kpis"></div>
    <div class="grid">
      <div class="card"><h2>Temperatura y humedad</h2><canvas id="c1"></canvas></div>
      <div class="card"><h2>Riesgo</h2><div class="ring" id="ring" style="--p:0;--c:#2b8b69"><i id="score">--</i></div><p id="risk" class="sub"></p></div>
      <div class="card"><h2>VPD y evaporacion</h2><canvas id="c2"></canvas></div>
      <div class="card"><h2>Alertas</h2><div id="alerts"></div><button id="alarm" class="arm" type="button">Apagar alarma</button><p id="alarmmsg" class="sub"></p></div>
      <div class="card"><h2>Presion</h2><canvas id="c3"></canvas></div>
      <div class="card"><h2>Luz</h2><canvas id="c4"></canvas></div>
    </div>
  </section>
  <section id="live" hidden>
    <div class="kpis" id="liveKeys"></div>
    <div class="card"><h2 id="liveName">Temperatura</h2><b id="liveVal" style="font-size:40px">--</b><canvas id="cLive"></canvas></div>
  </section>
  <section id="hist" hidden><div class="card scroll"><table id="tbl"></table></div></section>
  <section id="dev" hidden><div class="kpis" id="devk"></div><div class="card" id="tools"><button id="base" type="button">Guardar base</button><label>Intervalo <select id="intv"><option value="0">Manual</option><option value="15">15 min</option><option value="60">1 h</option><option value="360">6 h</option></select></label><button id="heart" type="button">Corazon</button><button id="moff" type="button">Apagar matriz</button><button id="alarm2" class="arm" type="button">Apagar alarma</button><p id="toolmsg" class="sub"></p></div></section>
</main>
<script>
const META=[['temperatura','Temperatura','°C',1],['humedad','Humedad','%',1],['presion','Presion','hPa',1],['distancia','Distancia','cm',1],['luz','Luz','lx',0],['vpd','VPD','kPa',2],['evaporacion','Evaporacion','%',1],['score','Riesgo','%',0]];
const TH={temperatura:[8,38],humedad:[20,95],presion:[650,850],vpd:[0.1,2.2],evaporacion:[null,75],score:[null,75]};
let page='home',liveKey='temperatura',last=null,rows=[],serie=[];
function esc(s){return String(s??'').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
function n(v,d){return Number.isFinite(+v)?(+v).toFixed(d):'--'}
function horaDe(t){if(t>1700000000)return new Date(t*1000).toLocaleString('es-CO');if(t>0)return 'T+'+t+'s';return 'ahora'}
function luxOf(r){return Number.isFinite(+r.lux)?+r.lux:(+r.luz||0)*1.5}
function mapLive(d){return{t:+d.epoch||0,temperatura:+d.temperatura,humedad:+d.humedad,presion:+d.presion,distancia:+d.distancia,luz:luxOf(d),vpd:+d.vpd,evaporacion:+d.evaporacion,score:+d.scoreRiesgo,raw:d}}
function mapHist(r){return{t:+r.t||0,temperatura:+r.temp,humedad:+r.hum,presion:+r.pres,distancia:+r.dist,luz:(+r.luz||0)*1.5,vpd:+r.vpd,evaporacion:+r.evap,score:+r.score}}
function alertsOf(s){const out=[];for(const k in TH){const v=s[k],lim=TH[k];if(!Number.isFinite(v))continue;const label=META.find(m=>m[0]===k)[1];if(lim[0]!=null&&v<lim[0])out.push(label+' por debajo de '+lim[0]);if(lim[1]!=null&&v>lim[1])out.push(label+' por encima de '+lim[1]);}return out}
function line(id,series){const cv=document.getElementById(id);if(!cv)return;const dpr=2,w=cv.width=(cv.clientWidth||320)*dpr,h=cv.height=180*dpr,c=cv.getContext('2d');c.clearRect(0,0,w,h);series.forEach(s=>{const vals=s.v.filter(v=>Number.isFinite(v));if(vals.length<2)return;let min=Math.min.apply(null,vals),max=Math.max.apply(null,vals);if(min===max){min-=1;max+=1}c.beginPath();c.strokeStyle=s.c;c.lineWidth=2.4*dpr;let started=false;s.v.forEach((v,i)=>{if(!Number.isFinite(v)){started=false;return}const x=12+i/(s.v.length-1)*(w-24),y=h-12-(v-min)/(max-min)*(h-24);if(started)c.lineTo(x,y);else{c.moveTo(x,y);started=true}});c.stroke()})}
function show(name){page=name;document.querySelectorAll('main section').forEach(s=>{s.hidden=s.id!==name});document.querySelectorAll('nav button[data-p]').forEach(b=>b.classList.toggle('on',b.dataset.p===name));draw()}
function draw(){if(!last)return;const s=last;document.getElementById('kpis').innerHTML=META.map(m=>'<div class="kpi"><span>'+m[1]+'</span><b>'+n(s[m[0]],m[3])+' <small>'+m[2]+'</small></b></div>').join('');const sc=Math.max(0,Math.min(100,+s.score||0));const col=sc<30?'#2b8b69':sc<60?'#bb8c31':sc<80?'#c76b2f':'#bf4848';const ring=document.getElementById('ring');ring.style.setProperty('--p',sc);ring.style.setProperty('--c',col);document.getElementById('score').textContent=Math.round(sc);document.getElementById('risk').textContent=esc((s.raw&&s.raw.riesgo)||'');const a=alertsOf(s);document.getElementById('alerts').innerHTML=a.length?a.map(t=>'<div class="alert"><b>'+esc(t)+'</b></div>').join(''):'<p class="sub">Sin alertas activas.</p>';const tail=serie.slice(-48);line('c1',[{c:'#2e6f61',v:tail.map(r=>r.temperatura)},{c:'#728eb8',v:tail.map(r=>r.humedad)}]);line('c2',[{c:'#a07a4d',v:tail.map(r=>r.vpd)},{c:'#8a6aa6',v:tail.map(r=>r.evaporacion)}]);line('c3',[{c:'#2e6f61',v:tail.map(r=>r.presion)}]);line('c4',[{c:'#a07a4d',v:tail.map(r=>r.luz)}]);const m=META.find(x=>x[0]===liveKey);document.getElementById('liveName').textContent=m[1];document.getElementById('liveVal').textContent=n(s[liveKey],m[3])+' '+m[2];line('cLive',[{c:'#1d5c4f',v:tail.map(r=>r[liveKey])}]);document.getElementById('tbl').innerHTML='<tr><th>Hora</th>'+META.map(x=>'<th>'+x[1]+'</th>').join('')+'</tr>'+rows.slice().reverse().slice(0,60).map(r=>'<tr><td>'+(horaDe(r.t))+'</td>'+META.map(x=>'<td>'+n(r[x[0]],x[3])+'</td>').join('')+'</tr>').join('');const d=s.raw||{};const up=+d.uptime||0;const cells=[['WiFi',d.ssid||d.wifi],['IP',d.ip],['Senal',(d.rssi||0)+' dBm'],['Internet',d.internet],['BME280',d.bmeOK?'OK':'Error'],['AJ-SR04M',d.ajOK?'OK':'Error'],['TCS230',d.tcsOK?'OK':'Error'],['Riesgo',d.riesgo],['Alarma',d.alarmaRoja===false?'Apagada':'Encendida'],['Historico',(d.historyCount||rows.length)+' en EEPROM'],['Encendido',Math.floor(up/3600)+'h '+Math.floor((up%3600)/60)+'m'],['Matriz',d.matriz],['Intervalo',d.historyAutoMin?d.historyAutoMin+' min':'manual']];document.getElementById('devk').innerHTML=cells.map(x=>'<div class="kpi"><span>'+x[0]+'</span><b>'+esc(x[1]??'--')+'</b></div>').join('');document.getElementById('where').textContent=(d.ip?d.ip+' · ':'')+'pagina y datos en el Arduino';document.getElementById('sub').textContent=rows.length+' lecturas guardadas en la EEPROM del Arduino.';pintarAlarma()}
async function load(){try{let hist=[];try{const h=await(await fetch('/history?_='+Date.now())).json();hist=(h.records||[]).map(mapHist)}catch(e){}const d=await(await fetch('/data?_='+Date.now())).json();last=mapLive(d);rows=hist.filter(r=>r.t!==last.t);rows.push(last);serie.push(last);if(serie.length>120)serie=serie.slice(-120);const sel=document.getElementById('intv');if(sel&&document.activeElement!==sel)sel.value=String(d.historyAutoMin||0);document.getElementById('err').hidden=true;draw()}catch(e){const el=document.getElementById('err');el.hidden=false;el.textContent='Sin respuesta del Arduino.'}}
document.querySelectorAll('nav button[data-p]').forEach(b=>b.onclick=()=>show(b.dataset.p));
document.getElementById('liveKeys').innerHTML=META.map(m=>'<button type="button" data-k="'+m[0]+'">'+m[1]+'</button>').join('');
document.getElementById('liveKeys').onclick=e=>{const b=e.target.closest('button');if(!b)return;liveKey=b.dataset.k;draw()};
document.getElementById('med').onclick=async()=>{const b=document.getElementById('med');b.disabled=true;b.textContent='Midiendo...';try{const r=await fetch('/measure?_='+Date.now());const j=await r.json();if(!j.ok)throw new Error('no');document.getElementById('err').hidden=true}catch(e){const el=document.getElementById('err');el.hidden=false;el.textContent='No se guardo la medicion.'}b.disabled=false;b.textContent='Tomar medicion';load()};
async function tool(path){const el=document.getElementById('toolmsg');const sep=path.includes('?')?'&':'?';el.textContent='Enviando...';try{const r=await fetch(path+sep+'_='+Date.now());el.textContent=(await r.text()).trim();load()}catch(e){el.textContent='Sin respuesta.'}}
document.getElementById('base').onclick=()=>tool('/savebase');
function pintarAlarma(){const apagada=!!(last&&last.raw&&last.raw.alarmaRoja===false);['alarm','alarm2'].forEach(id=>{const b=document.getElementById(id);if(!b)return;b.textContent=apagada?'Activar alarma':'Apagar alarma';b.classList.toggle('arm',!apagada);});}
document.getElementById('heart').onclick=()=>tool('/heart');
document.getElementById('moff').onclick=()=>tool('/matrixoff');
async function alarma(apagar){const el=document.getElementById('alarmmsg');const tool=document.getElementById('toolmsg');el.textContent='Enviando...';if(tool)tool.textContent='Enviando...';try{const r=await fetch((apagar?'/alarmoff':'/alarmon')+'?_='+Date.now());const t=(await r.text()).trim();el.textContent=t;if(tool)tool.textContent=t;await load()}catch(e){el.textContent='Sin respuesta.';if(tool)tool.textContent='Sin respuesta.'}}
document.getElementById('alarm').onclick=()=>alarma(document.getElementById('alarm').textContent.indexOf('Apagar')===0);
document.getElementById('alarm2').onclick=()=>alarma(document.getElementById('alarm2').textContent.indexOf('Apagar')===0);
document.getElementById('intv').onchange=(e)=>tool('/setinterval?m='+e.target.value);
document.getElementById('theme').onclick=()=>{const d=document.documentElement;d.dataset.theme=d.dataset.theme==='dark'?'':'dark'};
load();setInterval(load,10000);
</script>
</body>
</html>

)PAGE";

inline void enviarDashboard(WiFiClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.println(F("Cache-Control: no-store"));
  client.println(F("Connection: close"));
  client.println();
  const size_t total = sizeof(AERIS_PAGE) - 1;
  size_t off = 0;
  while (off < total) {
    size_t n = total - off;
    if (n > 480) n = 480;
    client.write((const uint8_t *)AERIS_PAGE + off, n);
    off += n;
    delay(1);
  }
  client.flush();
}
