import socket
import fcntl
import struct
# import file_with_name_msg_pb2  # Generated from the .proto file

BINARY_NUMBER_LENGTH = 32
CHUNK_NUMBER = 2
CHUNK_LENGTH = 10

class Replica:
    def __init__(self, name: str, ip: str, port: int, chunk_id: int, is_primary: bool):
        self.name = name
        self.ip = ip
        self.port = port
        self.chunk_id = chunk_id
        self.is_primary = is_primary

def receive_message(sock, length):
    """Helper function to receive exactly 'length' bytes from the socket."""
    data = b""
    while len(data) < length:
        packet = sock.recv(length - len(data))
        if not packet:
            return None
        data += packet
    return data

def int_to_c_binary(n):
    return ''.join(str((n >> i) & 1) for i in range(31, -1, -1))

def binary_to_int(binary_str):
    return int(binary_str, 2)

def main():
    # Creating an array (list) of Replica objects
    replicas = [
        Replica("Replica1", "1.2.3.4", 8000, 1, True),
        Replica("Replica2", "1.2.3.4", 8081, 2, False),
        Replica("Replica3", "1.2.3.4", 8082, 3, False),
    ]

    SERVER_IP = replicas[0].ip
    SERVER_PORT = replicas[0].port

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client_socket:
        try:
            client_socket.connect((SERVER_IP, SERVER_PORT))
            print(f"Connected to replica {SERVER_IP}:{SERVER_PORT}")
            client_socket.setblocking(1)

            number_to_send = int_to_c_binary(CHUNK_NUMBER)
            client_socket.sendall((str(number_to_send)).encode())
            print("Message sent to relica (chunk number in binary format):", number_to_send)
            print("of length:", len(str(number_to_send)))

            response_length_message = receive_message(client_socket, BINARY_NUMBER_LENGTH)
            if response_length_message is None:
                print("None was received")
            if response_length_message is not None:
                print(response_length_message.decode() + " was received")
            
            response_length = binary_to_int(response_length_message)
            response_message = receive_message(client_socket, response_length)
            if response_message is None:
                print("None was received")
            if response_message is not None:
                print("Received message:",response_message.decode())

            chunk_data = receive_message(client_socket, CHUNK_LENGTH)
            if chunk_data is None:
                print("None was received")
            if chunk_data is not None:
                print("Received message:",chunk_data.decode())
        except socket.error as e:
            print(f"Socket error: {e}")
        except ConnectionError as ce:
            print(ce)
        except Exception as e:
            print(f"Error: {e}")

main()