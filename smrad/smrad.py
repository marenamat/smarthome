import socket
import struct

UDP_IP = "192.168.1.1"
UDP_PORT = 4242

sock = socket.socket(socket.AF_INET, # Internet
                     socket.SOCK_DGRAM) # UDP
sock.bind((UDP_IP, UDP_PORT))

while True:
    data, addr = sock.recvfrom(1024) # buffer size is 1024 bytes
    u = struct.unpack("HHBBBBI", data)
    print(f"received message: {data} interpreted as {u}")
