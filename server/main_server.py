import socket
import file_with_name_msg_pb2
import file_request_pb2
import replicas_response_pb2

# Parametry sieciowe
SERVER_IP = '127.0.0.1'
SERVER_PORT = 8000

# Parametry haszowania
BASE = 113
MOD = int(1e9) + 7

CHUNK_SIZE = int(1<<26)

def get_hash(path):
    if(type(path) != str or len(path) < 1):
        print("Error (in hashing function): Not a valid path.")
        return

    hash = ord(path[0])

    for i in range(1, len(path)):
        hash = (hash * BASE + ord(path[i])) % MOD
    
    # print("Hash:", hash)

    return hash

servers = []
file_map = dict(hashfunc=get_hash)

class Server:
    def __init__(self, name: str, ip: str, port: int):
        self.name = name # probably it's just the name of the file
        self.ip = ip
        self.port = port # port maybe should be excluded, because a computer could run multiple
        # chunkserver programs on different ports to boost performance

class Replica:
    # def __init__(self, name: str, ip: str, port: int, chunk_id: int, is_primary: bool):
    # def __init__(self, name: str, server: Server, chunk_id: int, is_primary: bool):
    def __init__(self, server: Server, chunk_id: int, is_primary: bool):
        # self.name = name
        # self.ip = ip
        # self.port = port
        self.server = server
        self.chunk_id = chunk_id
        self.is_primary = is_primary

def seed_servers():
    servers.append(
        Server("Server A", "127.0.0.1", 8000)
    )

    servers.append(
        Server("Server B", "127.0.0.2", 8000)
    )
    
    servers.append(
        Server("Server C", "127.0.0.3", 8000)
    )

def seed_files(paths):
    # print(servers[0])
    # print(paths[0])
    # print(paths[1])

    chunk_id_1 = get_hash(paths[0]) + 1
    chunk_id_2 = get_hash(paths[0]) + 2

    replica_list_1 = list()
    replica_list_1.append(
        Replica(servers[0], chunk_id_1, True), # the chunk id probably should be sth else,
        # either a global, incremented value or a hash of the path combined with the chunk number in a file
    )

    replica_list_1.append(
        Replica(servers[1], chunk_id_1, False),
    )

    replica_list_1.append(
        Replica(servers[2], chunk_id_1, False),
    )

    replica_list_2 = list()
    replica_list_2.append(
        Replica(servers[0], chunk_id_2, True), # the chunk id probably should be sth else,
        # either a global, incremented value or a hash of the path combined with the chunk number in a file
    )

    replica_list_2.append(
        Replica(servers[1], chunk_id_2, False),
    )

    replica_list_2.append(
        Replica(servers[2], chunk_id_2, False),
    )

    # print(chunk_id)

    file_map[paths[0]] = [replica_list_1, replica_list_2]

    # print(file_map)

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

def get_replicas(file_hash, path, offset, size):
    # print(CHUNK_SIZE)
    chunk_no = int(offset / CHUNK_SIZE)
    replica_list = file_map[path][chunk_no]
    # print(replica_list[0].server.ip)

    replicas_proto = []

    for replica in replica_list:
        print(replica.is_primary)
        replica_proto = replicas_response_pb2.Replica()
        replica_proto.name = path
        replica_proto.ip = replica.server.ip
        replica_proto.port = replica.server.port
        replica_proto.chunk_id = get_hash(path) + chunk_no
        replica_proto.is_primary = replica.is_primary
        replicas_proto.append(replica_proto)

    replicas_response = replicas_response_pb2.ReplicaList(replicas=replicas_proto)

    protobuf_data = replicas_response.SerializeToString()
    
    return protobuf_data

    # file_request.

def main():
    # print(get_hash("/home/piotr/Desktop/photo2.png"))
    seed_servers()

    file_1 = "/usr/piotr/Desktop/photo1.png"
    seed_files([file_1])

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_sock:
        server_sock.bind((SERVER_IP, SERVER_PORT))
        server_sock.listen(1)
        print(f"Server listening on {SERVER_IP}:{SERVER_PORT}")

        while True:
            conn, addr = server_sock.accept()
            with conn:
                print(f"Connection established with {addr}")

                receive_request(conn)

                protobuf_data = get_replicas(get_hash(file_1), file_1, 0, CHUNK_SIZE)

                msg_length = len(protobuf_data)
                conn.sendall(msg_length.to_bytes(4, byteorder='big'))

                # Wysłanie samej wiadomości
                conn.sendall(protobuf_data)
                print(f"Sent message to client.")


main()
