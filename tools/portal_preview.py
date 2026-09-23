"""Local preview API for UI QA only. Never used in the firmware or for device setup."""
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
CONFIG=dict(config_version=1,wifi_ssid='',has_password=False,monthly_salary=40000,work_days=31,work_start='09:00',lunch_start='12:00',lunch_end='13:00',work_end='18:00',timezone='Asia/Taipei',display_theme=0)

class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def reply(self,data,status=200):
        body=json.dumps(data).encode()
        self.send_response(status);self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(body)
    def do_GET(self):
        if self.path=='/':
            self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.end_headers()
            self.wfile.write((ROOT/'components/setup_portal/web/index.html').read_bytes())
        elif self.path=='/api/config':
            self.reply(CONFIG)
        elif self.path=='/api/scan':
            self.reply(dict(total=3,networks=[dict(ssid='Office',rssi=-43,security='WPA2/WPA3'),dict(ssid='<script>test</script>',rssi=-67,security='WPA2'),dict(ssid='',rssi=-80,security='Open')]))
        elif self.path=='/api/status':self.reply(dict(system_state='setup',ap_ssid='SalaryThief-A31F',ip='192.168.4.1',time_synced=False))
        else:self.reply(dict(error='Not found'),404)
    def do_POST(self):
        body=self.rfile.read(int(self.headers.get('Content-Length',0)))
        data=json.loads(body)
        if self.headers.get('X-SalaryThief-Request')!='setup':self.reply(dict(error='Header missing'),403);return
        if self.path=='/api/config':
            assert data['work_days']>0 and data['work_start']<data['lunch_start']<data['lunch_end']<data['work_end']
            assert type(data['display_theme']) is int and 0<=data['display_theme']<=2
            CONFIG.update({key:value for key,value in data.items() if key!='wifi_password'})
            if 'wifi_password' in data:CONFIG['has_password']=bool(data['wifi_password'])
            # Save only the names of submitted fields. Never log the supplied password.
            (ROOT/'.artifacts/portal-fields.json').write_text(json.dumps(sorted(data)),encoding='utf-8')
            self.reply(dict(saved=True,reboot_required=True))
        else:self.reply(dict(accepted=True),202)

if __name__=='__main__':
    print('Preview at http://127.0.0.1:8765',flush=True)
    ThreadingHTTPServer(('127.0.0.1',8765),Handler).serve_forever()
