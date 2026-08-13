#!/usr/bin/env python2.6

''' diamond mock '''

import sys,urllib
from os import curdir,sep
from BaseHTTPServer import BaseHTTPRequestHandler,HTTPServer


class DiamondHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        try:
            get=True
            key='mytest'
            value=''
            kw=self.path.split("&")
            for c in kw:
                 k = c.split("=")
                 if '?method' in k[0] and k[1] == 'set':
                    get=False
                 elif k[0] == 'key':
                    key=k[1]
                 elif k[0] == 'value':
                    value=k[1]
            if get == True:
                f=open(curdir+sep+key)
                self.send_response(200)
                self.send_header('Content-type','text/html')
                self.end_headers()
                self.wfile.write(f.read())
                f.close()
            else:
                f=open(curdir+sep+key, "w")
                self.send_response(200)
                self.send_header('Content-type','text/html')
                self.end_headers()
                content = urllib.unquote(value)
                f.write(content)
                f.close()
                self.wfile.write('ok')

        except IOError:
            self.send_error(404, 'File Not Found: %s' % self.path)

if __name__=='__main__':
    port=9000
    if len(sys.argv) == 2:
	 port=int(sys.argv[1])
    try:
        server = HTTPServer(('',port),DiamondHandler)
        server.serve_forever()
    except KeyboardInterrupt:
        server.socket.close()
