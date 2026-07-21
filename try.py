import socket

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("0.0.0.0", 8080))
server.listen(5)

print("等待设备连接：0.0.0.0:8080")

while True:
    client, address = server.accept()
    print("设备已连接：", address)

    try:
        while True:
            data = client.recv(1024)
            if not data:
                break

            print("收到：", data.decode(errors="replace"))

            # 示例：让设备打开 LED
            client.sendall(b"LED ON")
    finally:
        client.close()
        print("设备已断开")