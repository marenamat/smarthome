import datetime
import socket
import struct

UDP_IP = "192.168.1.1"
UDP_PORT = 4242

sock = socket.socket(socket.AF_INET, # Internet
                     socket.SOCK_DGRAM) # UDP
sock.bind((UDP_IP, UDP_PORT))

while True:
    data, addr = sock.recvfrom(1024) # buffer size is 1024 bytes
    u = dict(zip([
        "version",
        "temp",
        "hum",
        "mac",
        "temp_crc",
        "hum_crc",
        "status",
        "_u0",
        "counter",
        ],
        struct.unpack("HHH6sBBBBI", data)))
    u["now"] = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    u["humR"] = u["hum"] / 65535.0
    u["tempC"] = -45 + 175 * u["temp"] / 65535.0
    print(f"received message: {data} interpreted as {u}")
