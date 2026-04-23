import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# sock.sendto(b"Hello UDP", ("127.0.0.1", 17777))
sock.sendto(b"Hello UDP", ("7.25.185.227", 7777))

# 如果想接收回复
sock.settimeout(3)
try:
    data, addr = sock.recvfrom(4096)
    print(f"收到回复: {data} 来自 {addr}")
except socket.timeout:
    print("超时，没有收到回复")

sock.close()