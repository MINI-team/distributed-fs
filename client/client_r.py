import socket
# import file_with_name_msg_pb2  # Generated from the .proto file

def receive_message(sock, length):
    """Helper function to receive exactly 'length' bytes from the socket."""
    data = b""
    while len(data) < length:
        packet = sock.recv(length - len(data))
        if not packet:
            return None
        data += packet
    return data

class Replica:
    def __init__(self, name: str, ip: str, port: int, chunk_id: int, is_primary: bool):
        self.name = name
        self.ip = ip
        self.port = port
        self.chunk_id = chunk_id
        self.is_primary = is_primary


def main():
    # Creating an array (list) of Replica objects
    replicas = [
        Replica("Replica1", "192.168.0.38", 8000, 1, True),
        Replica("Replica2", "192.168.1.2", 8081, 2, False),
        Replica("Replica3", "192.168.1.3", 8082, 3, False),
    ]


    # Parametry
    SERVER_IP = replicas[0].ip
    SERVER_PORT = replicas[0].port

    # Tworzenie socketu TCP
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client_socket:
        try:
            # Tworzenie połączenia z serwerem
            client_socket.connect((SERVER_IP, SERVER_PORT))
            print(f"Connected to server {SERVER_IP}:{SERVER_PORT}")

            # Wysyłanie wiadomości
            message = "Give me a file please :)"
            client_socket.sendall(message.encode())
            print("Message sent to server:", message)

            # # Odbieranie rozmiaru wiadomości
            # raw_msg_length = receive_message(client_socket, 4)
            # if raw_msg_length is None:
            #     print("Error: No data received.")
            #     return

            # msg_length = int.from_bytes(raw_msg_length, byteorder='big')
            # print(f"Incoming message size: {msg_length} bytes")

            # # Odebranie właściwej wiadomości
            # protobuf_data = receive_message(client_socket, msg_length)
            # if protobuf_data is None:
            #     print("Error: No data received.")
            #     return

            # # Parsowanie wiadomości
            # file_response = file_with_name_msg_pb2.FileResponse()
            # file_response.ParseFromString(protobuf_data)

            # # Zapisanie pliku na dysk
            # with open(file_response.filename, 'wb') as f:
            #     f.write(file_response.filedata)
            #     print(f"File '{file_response.filename}' saved successfully.")

        except Exception as e:
            print(f"Error: {e}")

main()