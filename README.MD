# Distributed File System for Large Data Stream Ingestion

This project is a **distributed file system** designed for efficiently handling the **ingestion and retrieval of large files** by multiple concurrent clients. Inspired by the **Google File System (GFS)**, it emphasizes high performance, fault tolerance, consistency, and scalability for data storage.

<hr>

## Key Features

  * **Distributed Storage:** Data is spread across multiple servers, each managing local disk space.
  * **Chunk-Based System:** Files are subdivided into **chunks**, the basic units of data transfer.
  * **Data Replication:** Each chunk is replicated on **three** different servers (replicas) for reliability. The system remains operational even if two replicas of a chunk fail.
  * **Consistency Options:** Provides two write operations:
      * **Normal Write:** Faster, but offers weaker consistency guarantees.
      * **Write-Committed:** Slower, but guarantees that the chunk is successfully written to the required number of replicas, ensuring better consistency.
  * **High Throughput:** Prioritizes data throughput during design, utilizing concurrent operations on the client and efficient server architecture.

<hr>

## Architecture Overview

The system comprises three main components: **Master**, **Replica**, and **Client**.

### 1\. Master

The **Master** is the central coordinating node and the single point of contact for metadata.

  * **Role:** Coordinates all system activities, monitors data distribution, and stores all **metadata** (e.g., file namespace, chunk-to-replica mapping, primary replica location).
  * **Metadata Management:** Implemented with a list of active replicas and a hash table for fast lookups of chunk information. It handles replica allocation (using a round-robin algorithm for new chunks) and failure tracking.
  * **Communication:** Utilizes an event-driven TCP server with **epoll** for asynchronous multiplexing, allowing it to handle many concurrent client and replica connections efficiently.

### 2\. Replica

A **Replica** is the fundamental component for data storage.

  * **Role:** Stores the actual file **chunks** on its local disk and handles read/write requests.
  * **Data Storage:** Manages the reading and writing of chunks to the local disk.
  * **Replication:** If a replica receives a chunk from the client (acting as the primary replica), it uses a TCP client module (**Replicator**) to forward the chunk content to the other assigned replicas for that chunk.
  * **Communication:** Similar to the Master, it uses an event-driven TCP server with **epoll** to manage concurrent connections from clients or other replicas.

### 3\. Client

The **Client** is the user-facing application that initiates file operations.

  * **Role:** Connects the user to the distributed system, handling file I/O operations (read/write).
  * **Thread Pool:** Uses a **thread pool** (default of 8 threads) for concurrent communication with multiple Replicas, maximizing network interface utilization for faster data transfer.
  * **Master Communication:** Connects to the Master to get/update the list of chunks and their assigned replicas for a file operation.
  * **Disk Manager:** Manages reading from and writing to the user's local disk.
  * **Replica Communication:** Establishes parallel connections to replicas to transfer the actual chunk data. The client attempts to connect to the primary replica first, falling back to secondary replicas on failure.

<hr>

## Implementation and Deployment

### Technology Stack

  * **Language:** C
  * **Networking:** **TCP** for communication; Master and Replica servers use **epoll** for non-blocking I/O and event-driven architecture.
  * **Message definition and serialization:** **Protobuf** (Protocol Buffers) for structured, efficient communication between components (Master, Replica, Client).

### Deployment

The system components are packaged for easy deployment:

  * **Master & Replica:** Provided as **Docker images** for simple setup and scaling on server environments.
  * **Client:** Provided as a **statically linked binary** for lightweight and direct execution on the user's machine without external dependencies.

#### Running a Distributed Version

To run the system across separate machines:

1.  **Run Master:** Deploy the master Docker image on a reliable host:

    ```bash
    pc_m$ docker run -p <host_port>:<container_port> dfs-master ./server <port>
    ```

2.  **Run Replicas:** Deploy replica Docker images on machines with large storage capacity, linking them to the Master:

    ```bash
    pc_r$ docker run -p <host_port>:<container_port> dfs-replica ./replica <ip> <port> <master_ip> <master_port>
    ```

3.  **Run Client:** Execute the client binary, providing the Master's address and the desired operation (`read` or `write`):

    ```bash
    pc_c$ ./client <operation> <file_name> <master_ip> <master_port>
    ```

#### Running a Developmental Version

For single-machine setup, a `docker-compose.yml` file is provided to easily orchestrate and scale the Master and Replicas.

<hr>

## Reliability and Performance

The system was rigorously tested against functional and non-functional requirements, with a focus on reliability, performance, and scalability.

  * **Reliability Testing:** Scenarios included deactivating one or more replicas during single and parallel read/write operations and full system shutdowns/restarts to confirm the system's ability to maintain data integrity and availability through replication.
  * **Correctness:** File integrity was verified using the **MD5 algorithm** to ensure that read files matched their original content after being written to the distributed system.
  * **Scalability:** Performance tests demonstrated that the system scales well and it's performance is limited mostly by network bandwidth. For a single client, increasing the number of replicas (up to 6 replicas) slightly improved write times, after which the improvements diminish due to factors like replication overhead. Read speed was found to be primarily limited by the client's network interface bandwidth (see graph below). <img width="1098" height="621" alt="image" src="https://github.com/user-attachments/assets/5525fe98-5c35-4635-8905-b3640b3d61a0" />
 The main point of scaling such systems is to handle more clients at the same time, which we achieved as shown by the graph below. With 3 clients, the write times for 10 replicas are over 3 times lower than for 3 replicas, demonstrating that it is possible to provide the same per client performance if the system is scaled proportionately. <img width="711" height="543" alt="image" src="https://github.com/user-attachments/assets/b0d98eb6-f9eb-4641-81b7-3142844f6fee" />
