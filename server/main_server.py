import socket
import file_with_name_msg_pb2
import file_request_pb2

# Parametry
SERVER_IP = '127.0.0.1'
SERVER_PORT = 8000

servers = []

class Replica:
    def __init__(self, name: str, ip: str, port: int, chunk_id: int, is_primary: bool):
        self.name = name
        self.ip = ip
        self.port = port
        self.chunk_id = chunk_id
        self.is_primary = is_primary

class Server:
    def __init__(self, name: str, ip: str, port: int):
        self.name = name
        self.ip = ip
        self.port = port # port maybe should be excluded, because a computer could run multiple
        # chunkserver programs on different ports to boost performance

def seed_servers():
    servers.append(
        Server("Server A", "127.0.0.1", 8000)
    )

    servers.append(
        Server("Server B", "127.0.0.1", 8000)
    )
    
    servers.append(
        Server("Server C", "127.0.0.1", 8000)
    )

def receive_message(sock, length):
    """Helper function to receive exactly 'length' bytes from the socket."""
    data = b""
    while len(data) < length:
        packet = sock.recv(length - len(data))
        if not packet:
            return None
        data += packet
    return data

def receive_request(sock):
    raw_msg_length = receive_message(sock, 4)

    if raw_msg_length is None:
        print("Error: No data received.")
        return

    msg_length = int.from_bytes(raw_msg_length, byteorder='big')
    print(f"Incoming message size: {msg_length} bytes")

    protobuf_data = receive_message(sock, msg_length)
    if protobuf_data is None:
        print("Error: No data received.")
        return

    file_request = file_request_pb2.FileRequest()
    file_request.ParseFromString(protobuf_data)

    print(file_request)

def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_sock:
        server_sock.bind((SERVER_IP, SERVER_PORT))
        server_sock.listen(1)
        print(f"Server listening on {SERVER_IP}:{SERVER_PORT}")

        while True:
            conn, addr = server_sock.accept()
            with conn:
                print(f"Connection established with {addr}")

                receive_request(conn)

main()
