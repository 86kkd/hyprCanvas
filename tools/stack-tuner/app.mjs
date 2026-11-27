import {DEFAULT,valid,adjust,currentPlugin,layout,payload,fromPayload} from './model.mjs';
const $=id=>document.getElementById(id), keys=['main','secondary','others'], names=['主窗口','次窗口','其他窗口'], tones=['var(--main)','var(--secondary)','var(--other)'];
let state={...DEFAULT}, active=2, previous=1, saved=null, timer;
const storageKey='hyprcanvas.stack-tuner.v3';
const letter=i=>String.fromCharCode(65+i), json=s=>JSON.stringify(payload(s));
function toast(message){$('toast').textContent=message;$('toast').classList.add('visible');clearTimeout(timer);timer=setTimeout(()=>$('toast').classList.remove('visible'),2600);}
function draft(){try{localStorage.setItem(storageKey,JSON.stringify(state));}catch{} render();}
function focus(i){if(i<0||i>=state.count||i===active)return;previous=active;active=i;render();}
function renderPreview(){
 const deck=layout(state,active,previous);active=deck.active;previous=deck.previous;
 $('canvas').className='canvas'+(state.direction==='column'?' column':'')+(!state.ruler?' no-ruler':'');
 const horizontal=state.direction==='column', extent=horizontal?$('stage').clientWidth:$('stage').clientHeight;
 for(const c of deck.cards){
  let el=$('window-'+c.index);
  if(!el){el=document.createElement('button');el.id='window-'+c.index;el.addEventListener('click',()=>focus(c.index));
   el.innerHTML=`<div class="window-chrome"><span><span class="window-letter">${letter(c.index)}</span><span class="window-title">workspace / ${['terminal','browser','editor','notes','files'][c.index%5]}</span></span><span class="window-controls">− □ ×</span></div><div class="editor">${[75,42,63,85,55,68,36,78,62,49,70,40].map((w,i)=>`<span class="line-no">${i+1}</span><span class="code-line ${i%4===0?'accent':''}" style="width:${w}%"></span>`).join('')}</div><span class="tag"></span>`;
   $('stage').append(el);
  }
  el.className='window '+c.kind+(c.kind==='other'?' narrow':'');el.setAttribute('aria-label',`${letter(c.index)} 窗口，${c.kind==='main'?'主窗口':c.kind==='secondary'?'次窗口':'窄边'}`);el.setAttribute('aria-pressed',String(c.index===active));
  const inset=c.insetPercent;
  el.style.cssText=horizontal?`left:${c.offset}%;width:${c.span}%;top:${inset}%;height:${c.crossPercent}%;z-index:${c.z}`:`top:${c.offset}%;height:${c.span}%;left:${inset}%;width:${c.crossPercent}%;z-index:${c.z}`;
  const tag=el.querySelector('.tag');tag.hidden=c.kind!=='other'||!state.labels;tag.textContent=`${letter(c.index)} · ${['Terminal','Browser','Editor','Notes','Files'][c.index%5]}`;tag.className='tag'+(horizontal?' vertical'+(!c.before?' right':''):!c.before?' bottom':'');
 }
 for(const el of [...$('stage').children])if(Number(el.id.split('-')[1])>=state.count)el.remove();
 $('measure').innerHTML=deck.segments.map(c=>{const k=c.kind==='main'?0:c.kind==='secondary'?1:2;return `<div class="ruler-segment" style="--tone:${tones[k]};${horizontal?'left':'top'}:${c.start}%;${horizontal?'width':'height'}:${c.visible}%" title="${letter(c.index)}：${c.visible.toFixed(1)}%"><span>${c.kind!=='other'?`${names[k]}<br><strong>${c.visible}%</strong><small>${(c.visible*state.extent/100).toFixed(0)} px</small>`:''}</span></div>`}).join('');
 $('previous').disabled=active===0;$('next').disabled=active===state.count-1;
 $('cross-summary').textContent=deck.cards.map(c=>letter(c.index)+' '+c.crossPercent.toFixed(1)+'%').join(' · ');
 $('focus-caption').innerHTML=`焦点 <span class="focus-token active">${letter(active)}</span> 次窗口 <span class="focus-token">${letter(previous)}</span>`;
}
function render(){
 const crossKeys=['crossMain','crossSecondary','crossOthers'];
 keys.forEach((k,i)=>{
  for(const suffix of ['number','range'])$('cross-'+k+'-'+suffix).value=state[crossKeys[i]];
  const r=$('cross-'+k+'-range');r.style.setProperty('--fill',((state[crossKeys[i]]-Number(r.min))/(Number(r.max)-Number(r.min))*100)+'%');
 });
 $('cross-description').textContent=state.direction==='row'?'纵向堆叠：分别设置三类卡片的横向宽度倍率。':'横向堆叠：分别设置三类卡片的竖向高度倍率。';
 $('cross-summary').textContent='主窗口 '+state.crossMain.toFixed(2)+'× · 次窗口 '+state.crossSecondary.toFixed(2)+'× · 其他每层 ×'+state.crossOthers.toFixed(2)+'×';

 keys.forEach((k,i)=>{for(const suffix of ['number','range'])$(k+'-'+suffix).value=state[k];const r=$(k+'-range');r.style.setProperty('--fill',`${(state[k]-Number(r.min))/(Number(r.max)-Number(r.min))*100}%`);});
 $('share-bar').innerHTML=keys.map(k=>`<span style="width:${state[k]}%">${state[k]>=8?state[k]+'%':''}</span>`).join('');
 $('summary').innerHTML=keys.map((k,i)=>`<span style="--tone:${tones[i]}">${names[i]} <b>${state[k]}%</b></span>`).join('');
 $('count').value=state.count;$('extent').value=state.extent;$('labels').checked=state.labels;$('ruler').checked=state.ruler;
 ['row','column'].forEach(k=>$(k).setAttribute('aria-pressed',String(state.direction===k)));
 $('extent-label').textContent=state.direction==='row'?'预览高度':'预览宽度';
 const px=state.others*state.extent/100/(state.count-2);$('peek-size').textContent=`${px.toFixed(1)} px`;$('peek-hint').textContent=px<24?'窄边不足 24 px，可提高其他窗口占比以方便点选。':'其他窗口均分总占比；像素按预览轴长度计算。';
 document.querySelectorAll('[data-preset]').forEach(b=>b.classList.toggle('selected',b.dataset.preset===keys.map(k=>state[k]).join(',')));
 const same=saved&&json(state)===json(saved);$('status').textContent=same?'已保存到项目 · final-ratios.json':saved?'有未保存的修改':'尚未保存最终参数';$('status').className=same?'saved':'';
 renderPreview();
}
keys.forEach(k=>{for(const suffix of ['number','range'])$('cross-'+k+'-'+suffix).addEventListener(suffix==='range'?'input':'change',e=>{
 if(e.target.value!==''&&Number.isFinite(Number(e.target.value))){
  const field={main:'crossMain',secondary:'crossSecondary',others:'crossOthers'}[k];
  state[field]=Math.round(Math.max(0.1,Math.min(k==='main'?2:1,Number(e.target.value)))*100)/100;
 }
 draft();
});});
keys.forEach(k=>{ $(k+'-range').addEventListener('input',e=>{state=adjust(state,k,e.target.value);draft();});$(k+'-number').addEventListener('change',e=>{if(e.target.value!=='')state=adjust(state,k,e.target.value);draft();});});
document.querySelectorAll('[data-preset]').forEach(b=>b.onclick=()=>{b.dataset.preset.split(',').forEach((v,i)=>state[keys[i]]=Number(v));draft();});
$('baseline').onclick=()=>{state=currentPlugin(state);draft();toast('已载入当前插件的 61.8 / 19.1 / 19.1 比例');};
$('reset').onclick=()=>{state={...DEFAULT};active=2;previous=1;draft();};
$('count').onchange=e=>{state.count=Number(e.target.value);draft();};
$('extent').onchange=e=>{state.extent=Math.max(400,Math.min(2400,Math.round(Number(e.target.value)||900)));draft();};
['row','column'].forEach(k=>$(k).onclick=()=>{state.direction=k;draft();});
['labels','ruler'].forEach(k=>$(k).onchange=e=>{state[k]=e.target.checked;draft();});
$('previous').onclick=()=>focus(active-1);$('next').onclick=()=>focus(active+1);
$('canvas').addEventListener('keydown',e=>{const d={ArrowUp:-1,ArrowLeft:-1,ArrowDown:1,ArrowRight:1}[e.key];if(d){e.preventDefault();focus(active+d);}});
$('save').onclick=async()=>{const snapshot={...state};$('save').disabled=true;try{const r=await fetch('/api/final',{method:'POST',headers:{'Content-Type':'application/json'},body:json(snapshot)});if(!r.ok)throw Error('保存失败，请确认本地服务仍在运行');saved=fromPayload(await r.json());render();toast('最终参数已保存到项目');}catch(e){$('status').textContent=e.message;$('status').className='error';}finally{$('save').disabled=false;}};
$('copy').onclick=async()=>{try{await navigator.clipboard.writeText(JSON.stringify(payload(state),null,2));toast('已复制参数');}catch{toast('浏览器未允许复制，请使用导出 JSON');}};
$('export').onclick=()=>{const url=URL.createObjectURL(new Blob([JSON.stringify(payload(state),null,2)+'\n'],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download='stack-ratios.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};
try{const r=await fetch('/api/final');if(!r.ok)throw Error();saved=fromPayload(await r.json());if(saved)state={...saved};}catch{toast('无法读取项目参数，请通过本地服务打开此页');}
try{let d=JSON.parse(localStorage.getItem(storageKey));
 if(!d){const old=JSON.parse(localStorage.getItem('hyprcanvas.stack-tuner.v2')||localStorage.getItem('hyprcanvas.stack-tuner.v1'));if(old)d={...old,crossMain:old.crossMain>=0.1&&old.crossMain<=2?old.crossMain:DEFAULT.crossMain,crossSecondary:DEFAULT.crossSecondary,crossOthers:DEFAULT.crossOthers};}
if(valid({...DEFAULT,...d})){state={...DEFAULT,...d};delete state.crossStep;}}catch{}
render();new ResizeObserver(renderPreview).observe($('canvas'));
