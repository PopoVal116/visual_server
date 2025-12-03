import zmq

context = zmq.Context()
socket = context.socket(zmq.REP)
socket.bind("tcp://*:5556")

print("ZMQ сервер запущен на порту 5556")

file = open("from_android.txt", "a", encoding="utf-8")
packet_count = 0

while True:
    message = socket.recv_string()
    packet_count += 1

    file.write(f"Пакет #{packet_count}: {message}\n")
    file.flush()

    print(f"Получен пакет #{packet_count}: {message}")
    socket.send_string(f"OK! Получено пакетов: {packet_count}")

file.close()