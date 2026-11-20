#!/usr/bin/env python3
"""Loopback-only stack ratio editor; no external dependencies."""
import argparse
import json
import math
import os
from pathlib import Path
import tempfile
from datetime import datetime, timezone
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

ROOT = Path(__file__).resolve().parent

def validate(p):
    if not isinstance(p, dict) or p.get('schema_version') != 1:
        raise ValueError('Invalid schema')
    r, v = p['ratios'], p['preview']
    cross = p.get('cross_axis', {})
    x, y, z = (cross.get(k, default) for k, default in zip(
        ('main_scale', 'secondary_relative_scale', 'others_decay'), (1, 0.95, 0.9)))
    if not all(type(n) in (float, int) and math.isfinite(n) for n in (x, y, z)):
        raise ValueError('Invalid cross axis ratios')
    if not (0.1 <= x <= 2 and 0.1 <= y <= 1 and 0.1 <= z <= 1):
        raise ValueError('Invalid progressive scales')
    a, b, c = (r[k] for k in ('main_percent', 'secondary_percent', 'others_total_percent'))
    if not all(type(n) in (float, int) and math.isfinite(n) for n in (a,b,c)):
        raise ValueError('Invalid ratios')
    if not (a >= 35 and 5 <= b <= a and 1 <= c <= 30 and abs(a+b+c-100) < .05):
        raise ValueError('Ratios must sum to 100')
    if not (type(v['window_count']) is int and 3 <= v['window_count'] <= 10 and type(v['extent_px']) is int and 400 <= v['extent_px'] <= 2400):
        raise ValueError('Invalid preview dimensions')
    if v['direction'] not in ('row','column') or any(type(v[k]) is not bool for k in ('labels','ruler')):
        raise ValueError('Invalid preview settings')
    return dict(cross_axis=dict(basis='progressive_cross_axis',main_scale=x,secondary_relative_scale=y,others_decay=z),schema_version=1, ratio_basis='visible_extent_along_stack_axis', ratios=dict(zip(('main_percent','secondary_percent','others_total_percent'),(a,b,c))),
        other_windows=dict(distribution='equal',count_policy='keep_total_share'),focus=dict(order='preserve_original',secondary='previous_focused_window',wrap=False),
        preview={k:v[k] for k in ('window_count','direction','extent_px','labels','ruler')})

class Handler(BaseHTTPRequestHandler):
    def reply(self, code, data, mime='application/json'):
        if not isinstance(data, bytes): data=json.dumps(data,ensure_ascii=False).encode()
        self.send_response(code); self.send_header('Content-Type',mime); self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data)
    def allowed(self):
        hosts={f'127.0.0.1:{self.server.server_port}',f'localhost:{self.server.server_port}'}
        return self.headers.get('Host') in hosts and self.headers.get('Origin',f'http://{self.headers.get("Host")}') in {f'http://{h}' for h in hosts}
    def do_GET(self):
        if not self.allowed(): return self.reply(403,{'error':'Local origin required'})
        path=self.path.split('?')[0]
        if path=='/api/final':
            try: return self.reply(200,json.loads(self.server.state_file.read_text()))
            except FileNotFoundError: return self.reply(200,None)
            except (ValueError,OSError): return self.reply(500,{'error':'Cannot read saved parameters'})
        files={'/':('index.html','text/html; charset=utf-8'),'/index.html':('index.html','text/html; charset=utf-8'),'/style.css':('style.css','text/css'),'/app.mjs':('app.mjs','text/javascript'),'/model.mjs':('model.mjs','text/javascript')}
        if path not in files:return self.reply(404,{'error':'Not found'})
        name,mime=files[path];self.reply(200,(ROOT/name).read_bytes(),mime)
    def do_POST(self):
        if not self.allowed():return self.reply(403,{'error':'Local origin required'})
        if self.path!='/api/final':return self.reply(404,{'error':'Not found'})
        try:
            size=int(self.headers.get('Content-Length','0'))
            if not 0<size<=16384 or self.headers.get('Content-Type')!='application/json':raise ValueError('Invalid request')
            data=validate(json.loads(self.rfile.read(size)))
            data['saved_at']=datetime.now(timezone.utc).isoformat()
        except (ValueError,KeyError,TypeError):return self.reply(400,{'error':'Invalid parameters'})
        name=None
        try:
            with tempfile.NamedTemporaryFile(mode='w',dir=self.server.state_file.parent,delete=False,encoding='utf-8') as f:
                name=f.name;json.dump(data,f,ensure_ascii=False,indent=2);f.write('\n')
            os.replace(name,self.server.state_file)
            self.reply(200,data)
        except OSError:
            if name:Path(name).unlink(missing_ok=True)
            self.reply(500,{'error':'Cannot save parameters'})

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--state-file',type=Path,default=ROOT/'final-ratios.json',help='Override only for isolated testing')
    args=parser.parse_args()
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler);server.state_file=args.state_file.resolve()
    print(f'堆叠调节台：http://127.0.0.1:{server.server_port}\n保存位置：{server.state_file}',flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:server.server_close()
