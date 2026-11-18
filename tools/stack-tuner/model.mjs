export const DEFAULT = Object.freeze({main:61.8,secondary:19.1,others:19.1,count:5,direction:'row',extent:900,labels:true,ruler:true});
const round = n => Math.round(n * 10) / 10;
const clamp = (n, a, b) => Math.max(a, Math.min(b,n));
export function valid(s) {
  return s && ['main','secondary','others','extent','count'].every(k=>typeof s[k]==='number'&&Number.isFinite(s[k]))
    && s.main>=35 && s.secondary>=5 && s.others>=1 && s.others<=30 && s.secondary<=s.main
    && Math.abs(s.main+s.secondary+s.others-100)<.05
    && Number.isInteger(s.count)&&s.count>=3&&s.count<=10
    && Number.isInteger(s.extent)&&s.extent>=400&&s.extent<=2400
    && ['row','column'].includes(s.direction)&&typeof s.labels==='boolean'&&typeof s.ruler==='boolean';
}
export function adjust(s,key,input) {
  const n=Number(input); if (!Number.isFinite(n)) return {...s};
  let {main,secondary,others}=s;
  if(key==='main') {
    main=round(clamp(n,35,94));
    others=round(clamp(others,Math.max(1,100-2*main),Math.min(30,95-main)));
    secondary=round(100-main-others);
  } else if(key==='secondary') {
    secondary=round(clamp(n,5,(100-others)/2));
    main=round(100-secondary-others);
  } else if(key==='others') {
    others=round(clamp(n,1,30));
    main=round(clamp(main,Math.max(35,(100-others)/2),95-others));
    secondary=round(100-main-others);
  }
  if (secondary>main) { main=round(main+.1); secondary=round(secondary-.1); }
  return {...s,main,secondary,others};
}
export function currentPlugin(s) {
  return {...s,main:61.8,secondary:19.1,others:19.1};
}
export function layout(s,active,previous) {
  const count=s.count;
  active=clamp(active,0,count-1);
  previous=previous>=0&&previous<count&&previous!==active?previous:active>0?active-1:1;
  const peek=s.others/(count-2);
  const exposure=i=>i===active?s.main:i===previous?s.secondary:peek;
  let offset=0;
  const cards=Array.from({length:count},(_,i)=>{
    if(i>0) offset+=exposure(i<=active?i-1:i);
    const distance=Math.abs(i-active);
    return {index:i,offset,span:s.main,visible:exposure(i),kind:i===active?'main':i===previous?'secondary':'other',
      before:i<active,inset:distance*12,z:i===active?count+1:i<active?i+1:count-i};
  });
  let cursor=0;
  const segments=cards.map(c=>{const segment={...c,start:cursor};cursor+=c.visible;return segment;});
  return {cards,segments,active,previous,peek};
}
export function payload(s) {
  return {schema_version:1,ratio_basis:'visible_extent_along_stack_axis',
    ratios:{main_percent:s.main,secondary_percent:s.secondary,others_total_percent:s.others},
    other_windows:{distribution:'equal',count_policy:'keep_total_share'},
    focus:{order:'preserve_original',secondary:'previous_focused_window',wrap:false},
    preview:{window_count:s.count,direction:s.direction,extent_px:s.extent,labels:s.labels,ruler:s.ruler}};
}
export function fromPayload(p) {
  const s={main:p?.ratios?.main_percent,secondary:p?.ratios?.secondary_percent,others:p?.ratios?.others_total_percent,
    count:p?.preview?.window_count,direction:p?.preview?.direction,extent:p?.preview?.extent_px,
    labels:p?.preview?.labels,ruler:p?.preview?.ruler};
  return p?.schema_version===1&&valid(s)?s:null;
}
