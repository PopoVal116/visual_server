import socket

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind(('0.0.0.0', 12345))
s.listen(1)
print("Сервер ждёт подключений...")

conn, addr = s.accept()
print(f"Подключился: {addr}")

data = conn.recv(1024)
print(f"Получено от клиента: {data.decode()}")

conn.send(b"Hello from Server!")
conn.close()
s.close()