import http.server as SimpleHTTPServer
import socketserver as SocketServer
import logging

PORT = 8080

class GetHandler(
        SimpleHTTPServer.SimpleHTTPRequestHandler
        ):

    def do_GET(self):
        logging.error(self.headers)
        SimpleHTTPServer.SimpleHTTPRequestHandler.do_GET(self)


Handler = GetHandler
httpd = SocketServer.TCPServer(("", PORT), Handler)

httpd.serve_forever()
