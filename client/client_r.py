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
        Replica("Replica1", "1.2.3.4", 8000, 1, True),
        Replica("Replica2", "1.2.3.4", 8081, 2, False),
        Replica("Replica3", "1.2.3.4", 8082, 3, False),
    ]
    chunk_nr = 2
    msg_length = 2
    chunk_length = 67108864

    SERVER_IP = replicas[0].ip
    SERVER_PORT = replicas[0].port

    # Tworzenie socketu TCP
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client_socket:
        try:
            # Tworzenie połączenia z serwerem
            client_socket.connect((SERVER_IP, SERVER_PORT))
            print(f"Connected to server {SERVER_IP}:{SERVER_PORT}")

            client_socket.sendall(str(chunk_nr).encode())
            print("Message sent to server:", chunk_nr)

            response_data = receive_message(client_socket, msg_length)
            if response_data.decode() == "no":
                print("Error: no ok")
                return
            chunk_data = receive_message(client_socket, chunk_length)
            print("Received message:",chunk_data.decode())
        except socket.error as e:
            print(f"Socket error: {e}")
        except ConnectionError as ce:
            print(ce)
        except Exception as e:
            print(f"Error: {e}")

main()