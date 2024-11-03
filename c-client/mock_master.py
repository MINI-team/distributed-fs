
import socket
import dfs_pb2  # Import the generated protobuf module

def create_chunk_list_response():
    # Create a ChunkList response with 3 Chunks, each with 1 Replica
    chunk_list = dfs_pb2.ChunkList()
    chunk_list.success = True

    for i in range(1, 4):  # Create 3 chunks
        chunk = chunk_list.chunks.add()  # Add a new Chunk to the list
        chunk.chunk_id = i

        # Create a single Replica for this Chunk
        replica = chunk.replicas.add()
        replica.name = f"Replica-{i}"
        replica.ip = "127.0.0.1"
        replica.port = 8000 + i
        replica.chunk_id = i
        replica.is_primary = (i == 1)  # Make the first chunk's replica the primary

    return chunk_list.SerializeToString()

def start_server():
    # Create a TCP/IP socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    # Bind the socket to the address and port
    server_address = ('localhost', 8000)
    server_socket.bind(server_address)
    
    # Listen for incoming connections
    server_socket.listen(1)
    print("Server listening on localhost:8000")
    
    while True:
        # Wait for a connection
        print("Waiting for a connection...")
        connection, client_address = server_socket.accept()
        
        try:
            print(f"Connection from {client_address} has been established.")
            
            # Receive the data in small chunks
            data = connection.recv(1024)  # Buffer size of 1024 bytes
            if data:
                # Deserialize the received data as a FileRequest
                file_request = dfs_pb2.FileRequest()
                file_request.ParseFromString(data)
                
                # Print the FileRequest fields
                print("Received FileRequest:")
                print(f"  Path: {file_request.path}")
                print(f"  Offset: {file_request.offset}")
                print(f"  Size: {file_request.size}")
                
                # Create a ChunkList response
                chunk_list_data = create_chunk_list_response()
                
                # Send the serialized ChunkList back to the client
                connection.sendall(chunk_list_data)
                print("Sent ChunkList response.")
            else:
                print("No more data from", client_address)
                
        finally:
            # Clean up the connection
            connection.close()
            print("Connection closed.")

if __name__ == "__main__":
    start_server()
