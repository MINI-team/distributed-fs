import socket
import file_with_name_msg_pb2  # Generated from the .proto file
# from . import file_request_pb2
import file_request_pb2
import replicas_response_pb2
from dataclasses import dataclass
from typing import List

SERVER_IP = "192.168.0.38"
SERVER_PORT = 8001
BINARY_NUMBER_LENGTH = 32
CHUNK_NUMBER = 2
CHUNK_LENGTH = 10
FILENAME = "40mb.jpg"

class Replica:
    def __init__(self, ip: str, port: int, chunk_id: int, is_primary: bool):
        self.ip = ip
        self.port = port
        self.chunk_id = chunk_id
        self.is_primary = is_primary

def receive_message(sock, length):
    """Helper function to receive exactly 'length' bytes from the socket."""
    if isinstance(length, bytes):
        length = int.from_bytes(length, "big")
    data = b""
    while len(data) < length:
        packet = sock.recv(length - len(data))
        if not packet:
            return None
        data += packet
    return data

def receive_len_and_message(socket):
    raw_msg_length = receive_message(socket, 4)
    if raw_msg_length is None:
        print("Error: No data received.")
        return

    msg_length = int.from_bytes(raw_msg_length, byteorder='big')
    print(f"Incoming message size: {msg_length} bytes")

    # Odebranie właściwej wiadomości
    protobuf_data = receive_message(socket, msg_length)
    if protobuf_data is None:
        print("Error: No data received.")
        return
    
    return protobuf_data

def simple_interaction():
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

            # Odbieranie rozmiaru wiadomości
            raw_msg_length = receive_message(client_socket, 4)
            if raw_msg_length is None:
                print("Error: No data received.")
                return

            msg_length = int.from_bytes(raw_msg_length, byteorder='big')
            print(f"Incoming message size: {msg_length} bytes")

            # Odebranie właściwej wiadomości
            protobuf_data = receive_message(client_socket, msg_length)
            if protobuf_data is None:
                print("Error: No data received.")
                return

            # Parsowanie wiadomości
            file_response = file_with_name_msg_pb2.FileResponse()
            file_response.ParseFromString(protobuf_data)

            # Zapisanie pliku na dysk
            with open(file_response.filename, 'wb') as f:
                f.write(file_response.filedata)
                print(f"File '{file_response.filename}' saved successfully.")

        except Exception as e:
            print(f"Error: {e}")

def int_to_c_binary(n):
    return ''.join(str((n >> i) & 1) for i in range(31, -1, -1))

def binary_to_int(binary_str):
    return int(binary_str, 2)

def send_file_request(path, offset, size):
    all_chunks_data = []
    message = file_request_pb2.FileRequest()
    message.path = path
    message.offset = offset
    message.size = size
    protobuf_message = message.SerializeToString()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client_socket:
        try:
            client_socket.connect((SERVER_IP, SERVER_PORT))

            msg_length = len(protobuf_message)
            client_socket.sendall(msg_length.to_bytes(4, byteorder='big'))

            client_socket.sendall(protobuf_message)
            print("Message sent to server:", message)

            protobuf_data = receive_len_and_message(client_socket)

            replicas_response = replicas_response_pb2.ReplicaList()
            replicas_response.ParseFromString(protobuf_data)

            print("Received message:", replicas_response)
            print("Success?", replicas_response.success)
            REPLICA_IP = replicas_response.replicas[0].ip
            REPLICA_PORT = replicas_response.replicas[0].port

            # hardcoded, later will be received from master
            replicas: List[Replica] = [
                Replica(ip="192.168.0.38", port=8080, chunk_id=0, is_primary=True),
                Replica(ip="192.168.0.38", port=8081, chunk_id=1, is_primary=False),
            ]
            #
            for replica in replicas:
                REPLICA_IP = replica.ip
                REPLICA_PORT = replica.port
                CHUNK_NUMBER = replica.chunk_id
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as replica_socket:
                    try:
                        replica_socket.connect((REPLICA_IP, REPLICA_PORT))
                        print(f"Connected to replica {REPLICA_IP}:{REPLICA_PORT}")
                        replica_socket.setblocking(1)

                        number_to_send = int_to_c_binary(CHUNK_NUMBER)
                        replica_socket.sendall((str(number_to_send)).encode())
                        print("Message sent to relica (chunk number in binary format):", number_to_send)
                        print("of length:", len(str(number_to_send)))

                        response_length_message = receive_message(replica_socket, BINARY_NUMBER_LENGTH)
                        if response_length_message is None:
                            print("None was received")
                        if response_length_message is not None:
                            print(response_length_message.decode() + " was received")

                        response_length = binary_to_int(response_length_message)
                        response_message = receive_message(replica_socket, response_length)
                        if response_message is None:
                            print("None was received")
                            return
                        if response_message is not None:
                            print("Received status:",response_message.decode())

                        if response_message == "not ok":
                            return

                        # to do: receive chunk length, it could be less than CHUNK_LENGTH
                        data_length = receive_message(replica_socket, BINARY_NUMBER_LENGTH)
                        response_length = binary_to_int(data_length)
                        print("Received data length: " + str(response_length))

                        chunk_data = receive_message(replica_socket, response_length)
                        if chunk_data is None:
                            print("None was received")
                        if chunk_data is not None:
                            print("Data was received")
                            all_chunks_data.append(chunk_data)
                    except socket.error as e:
                        print(f"Socket error: {e}")
                    except ConnectionError as ce:
                        print(ce)
                    except Exception as e:
                        print(f"Error: {e}")

        except Exception as e:
            print(f"Error: {e}")

def main():
    # send_file_request("/home/piotr/Pictures/gfs.png", 8000000, 2000000)
    send_file_request("/home/vlada/Documents/thesis/distributed-fs/server/gfs.png", 8000000, 2000000)
    # simple_interaction()

main()