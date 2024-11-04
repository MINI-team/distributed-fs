import sys
import os

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

import socket
import sys
# import file_request_pb2
# import replicas_response_pb2
import dfs_pb2

# Parametry sieciowe
SERVER_IP = "127.0.0.1"
SERVER_PORT = 8001

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

    return hash

class Server:
    def __init__(self, name: str, ip: str, port: int):
        self.name = name # probably it's just the name of the file
        self.ip = ip
        self.port = port # port maybe should be excluded, because a computer could run multiple
        # chunkserver programs on different ports to boost performance

class Replica:
    # def __init__(self, server: Server, chunk_id: int, is_primary: bool):
    def __init__(self, server: Server, is_primary: bool):
        self.server = server
        # self.chunk_id = chunk_id # moved to Chunk
        self.is_primary = is_primary

class Chunk:
    def __init__(self, chunk_id: int, replicas: list):
        self.chunk_id = chunk_id
        self.replicas = replicas

servers = []
file_map = dict(hashfunc=get_hash)

def seed_servers():
    servers.append(
        Server("Server A", "127.0.0.1", 8080)
    )

    servers.append(
        Server("Server B", "127.0.0.1", 8081)
    )
    
    servers.append(
        Server("Server C", "127.0.0.1", 8082)
    )

def seed_files(paths):
    # fills the file_map dictionary with one entry, the entries are of the format 
    # [Replica(ip, port, ...), Replica(ip, port...), Replica(ip, port...)]
    chunk_id_1 = 1
    chunk_id_2 = 2
    # the chunk id  should be a global, incremented value

    replica_list_1 = list()
    replica_list_1.append(
        Replica(servers[0], True)
    )

    replica_list_1.append(
        Replica(servers[1], False),
    )

    replica_list_1.append(
        Replica(servers[2], False),
    )

    replica_list_2 = list()
    replica_list_2.append(
        Replica(servers[0], True)
    )

    replica_list_2.append(
        Replica(servers[1], False),
    )

    replica_list_2.append(
        Replica(servers[2], False),
    )

    file_map[paths[0]] = [Chunk(chunk_id_1, replica_list_1), Chunk(chunk_id_2, replica_list_2)]

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

    file_request = dfs_pb2.FileRequest()
    file_request.ParseFromString(protobuf_data)

    print(file_request)

    return file_request

def get_replicas(file_hash, path, offset, size):
    chunk_no = int(offset / CHUNK_SIZE)

    if(path not in file_map):
        replicas_response = dfs_pb2.ChunkList(success=False, chunks=[])
        protobuf_data = replicas_response.SerializeToString()
        return protobuf_data
    
    chunks = []

    for chunk in file_map[path]:
        replicas = []
        replica_list = chunk.replicas

        for replica in replica_list:
            print("Is replica primary?", replica.is_primary)
            replica_proto = dfs_pb2.Replica()
            replica_proto.name = replica.server.name
            replica_proto.ip = replica.server.ip
            replica_proto.port = replica.server.port
            # replica_proto.chunk_id = get_hash(path) + chunk_no
            replica_proto.is_primary = replica.is_primary
            replicas.append(replica_proto)
        
        chunk = dfs_pb2.Chunk(chunk_id = chunk.chunk_id, replicas = replicas)
        chunks.append(chunk)

    chunk_list = dfs_pb2.ChunkList(success = 1, chunks = chunks)
    protobuf_data = chunk_list.SerializeToString()
    
    return protobuf_data

    # file_request.

def main():
    # print(get_hash("/home/piotr/Desktop/photo2.png"))
    seed_servers()

    # file_1 = "/home/piotr/Desktop/photo1.png"
    file_1 = "/home/vlada/Documents/thesis/distributed-fs/server/gfs.png"
    seed_files([file_1])

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_sock:
        conn = socket.socket()
        server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            server_sock.bind((SERVER_IP, SERVER_PORT))
            server_sock.listen(1)
            print(f"Server listening on {SERVER_IP}:{SERVER_PORT}")

            while True:
                conn, addr = server_sock.accept()
                with conn:
                    print(f"Connection established with {addr}")

                    req = receive_request(conn)

                    protobuf_data = get_replicas(get_hash(req.path), req.path, req.offset, req.size)

                    msg_length = len(protobuf_data)

                    # conn.sendall(msg_length.to_bytes(4, byteorder='big'))

                    # Wysłanie samej wiadomości i zakończenie połączenia
                    conn.sendall(protobuf_data)
                    conn.close()

                    print(f"Sent message to client.\nPress q to exit")
                    char = sys.stdin.read(1)
                    if(char == 'q'):
                        break
        except Exception as e:
            print(f"Errorxd: {e}")
        
        print("after catching exception (or successful try)")
        
        server_sock.close()
        conn.close()

main()