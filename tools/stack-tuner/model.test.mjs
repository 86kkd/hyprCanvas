import assert from 'node:assert/strict';
import {DEFAULT,adjust,valid,layout,currentPlugin,payload,fromPayload} from './model.mjs';
let s={...DEFAULT};
for(let i=0;i<20000;i++){s=adjust(s,['main','secondary','others'][i%3],(i*7919%11000)/100);assert.ok(valid(s),JSON.stringify(s));}
for(let count=3;count<=10;count++)for(let active=0;active<count;active++)for(let previous=0;previous<count;previous++){
 if(active===previous)continue;
 const state={...DEFAULT,count}, d=layout(state,active,previous);
 assert.equal(d.cards[active].kind,'main');assert.equal(d.cards[previous].kind,'secondary');
 assert.ok(Math.abs(d.segments.reduce((n,c)=>n+c.visible,0)-100)<1e-8);
 assert.ok(Math.abs(Math.max(...d.cards.map(c=>c.offset+c.span))-100)<1e-8);
 assert.ok(d.cards.every((c,i)=>i===0||c.offset>d.cards[i-1].offset));
}
assert.deepEqual(fromPayload(payload(DEFAULT)),DEFAULT);
assert.equal(fromPayload({}),null);
assert.equal(currentPlugin({...DEFAULT,count:5,extent:1000}).others,19.1);
assert.equal(currentPlugin({...DEFAULT,count:10,extent:1000}).others,19.1);
console.log('Model checks passed: balance, ordering, all focus pairs, baseline and serialization.');
