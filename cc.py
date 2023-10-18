# echo-client.py

import socket
import time

HOST = "192.168.50.229"  # The server's hostname or IP address
PORT = 7777  # The port used by the server

def ramp_up():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.connect((HOST, PORT))
        val = 0
        while val <= 255:
            s.sendall(bytes([101, val]))
            val += 1
            time.sleep(0.02)

def ramp_down():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.connect((HOST, PORT))
        val = 255
        while val >= 0:
            s.sendall(bytes([101, val]))
            val -= 1
            time.sleep(0.02)

def reset():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.connect((HOST, PORT))
        s.sendall(bytes([101, 0]))

reset()
input("press enter to ramp up")
ramp_up();
input("press enter to ramp down")
ramp_down();
