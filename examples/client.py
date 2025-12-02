import socket

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(('127.0.0.1', 12345))

s.send(b"Hello World!")
data = s.recv(1024)
print(f"Ответ от сервера: {data.decode()}")

s.close()