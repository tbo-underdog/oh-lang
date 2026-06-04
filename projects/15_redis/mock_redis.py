#!/usr/bin/env python3
# Minimal correct RESP server for testing the Oh Redis client (SET/GET).
import socket
def parse(b):
    if not b or b[0:1]!=b'*': return None,b
    try:
        i=b.index(b'\r\n'); n=int(b[1:i]); p=i+2; args=[]
        for _ in range(n):
            assert b[p:p+1]==b'$'; j=b.index(b'\r\n',p); ln=int(b[p+1:j]); p=j+2
            args.append(b[p:p+ln]); p+=ln+2
        return args,b[p:]
    except (ValueError,AssertionError,IndexError): return None,b
def main():
    s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    s.bind(('127.0.0.1',6379)); s.listen(4); store={}
    while True:
        c,_=s.accept(); buf=b''
        while True:
            d=c.recv(4096)
            if not d: break
            buf+=d
            while True:
                cmd,rest=parse(buf)
                if cmd is None: break
                buf=rest; v=cmd[0].upper()
                if v==b'SET': store[cmd[1]]=cmd[2]; c.send(b'+OK\r\n')
                elif v==b'GET':
                    x=store.get(cmd[1])
                    c.send(b'$-1\r\n' if x is None else b'$%d\r\n%s\r\n'%(len(x),x))
                else: c.send(b'-ERR unknown\r\n')
        c.close()
main()
