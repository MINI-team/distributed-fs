import socket
import file_with_name_msg_pb2  # Generated from the .proto file
# from . import file_request_pb2
import file_request_pb2

SERVER_IP = '127.0.0.1'
SERVER_PORT = 8000

def send_file_request(path, offset, size):
    message = file_request_pb2.FileRequest()
    message.path = path
    message.offset = offset
    message.size = size
    protobuf_message = message.SerializeToString()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client_socket:
        try:
            client_socket.connect((SERVER_IP, SERVER_PORT))
            client_socket.sendall(protobuf_message)
            print("Message sent to server:", message)
        
        except Exception as e:
            print(f"Error: {e}")

def main():
    send_file_request("/home/piotr/Pictures/gfs.png", 8000000, 2000000)

main()

def receive_message(sock, length):
    """Helper function to receive exactly 'length' bytes from the socket."""
    data = b""
    while len(data) < length:
        packet = sock.recv(length - len(data))
        if not packet:
            return None
        data += packet
    return data

def simple_interaction():
    # Parametry
    SERVER_IP = '127.0.0.1'
    SERVER_PORT = 8000

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