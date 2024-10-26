import socket
import file_with_name_msg_pb2

def main():
    # Parametry
    SERVER_IP = '127.0.0.1'
    SERVER_PORT = 8000

    # Tworzenie socketu TCP
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
        server_socket.bind((SERVER_IP, SERVER_PORT))
        server_socket.listen(1)
        print(f"Server listening on {SERVER_IP}:{SERVER_PORT}")

        while True:
            # Akceptowanie połączeń od klienta
            conn, addr = server_socket.accept()
            with conn:
                print(f"Connection established with {addr}")

                # Odebranie wiadomości od klienta
                client_message = conn.recv(1024).decode()
                print("Received message from client:", client_message)

                # Stworzenie i serializacja wiadomości protobuf
                file_response = file_with_name_msg_pb2.FileResponse()
                file_response.filename = FILENAME
                with open("gfs.png", "rb") as photo_file:
                    file_response.filedata = photo_file.read()

                protobuf_data = file_response.SerializeToString()

                # Wysłanie długości wiadomości
                msg_length = len(protobuf_data)
                conn.sendall(msg_length.to_bytes(4, byteorder='big'))

                # Wysłanie samej wiadomości
                conn.sendall(protobuf_data)
                print(f"Sent file '{file_response.filename}' to client.")

main()
