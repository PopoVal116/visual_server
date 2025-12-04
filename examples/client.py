import socket

client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client_socket.connect(('localhost', 12345))
for i in range (1, 11):
    message = f'Hello, server! Сообщение #{i}'.encode()
    client_socket.sendall(message)
    print(f"Сообщение {i} отправлено")
client_socket.close()
print("Сообщение отправлено")