import socket
# TCP server to communicate with the ESP32 modem over modem lte connection. 
# This server listens for incoming connections from the modem and exchanges data.    


HOST = "0.0.0.0"      # Listen on all interfaces
PORT = 5000           # Must match SERVER_PORT in your firmware

print("Starting TCP server...")

# Create TCP socket
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind((HOST, PORT))
server.listen(1)

print(f"Listening on port {PORT}...")

# Accept connection from modem
conn, addr = server.accept()
print(f"Connected by {addr}")

while True:
    # Receive data from modem
    data = conn.recv(1024)
    if not data:
        break

    print("Received from modem:", data.decode())

    # Send data back to modem
    reply = "Hello from server!"
    conn.sendall(reply.encode())
    print("Sent to modem:", reply)

conn.close()
