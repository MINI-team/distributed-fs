import subprocess
import time
import pytest
import socket # ADD WAIT FOR PORT
from pathlib import Path

def wait_for_port(port: int, host: str = 'localhost', timeout: float = 2.0):
    """Wait until a port starts accepting TCP connections.
    Args:
        port: Port number.
        host: Host address on which the port should exist.
        timeout: In seconds. How long to wait before raising errors.
    Raises:
        TimeoutError: The port isn't accepting connection after time specified in `timeout`.
    """
    start_time = time.perf_counter()
    while True:
        try:
            with socket.create_connection((host, port), timeout=timeout):
                break
        except OSError as ex:
            time.sleep(0.01)
            if time.perf_counter() - start_time >= timeout:
                raise TimeoutError('Waited too long for the port {} on host {} to start accepting '
                                   'connections.'.format(port, host)) from ex

@pytest.fixture
def build_and_start_services():
    replica_dir = Path("../mock-replica")
    client_dir = Path("../c-client")
    server_dir = Path("../server")
    # subprocess.run(["protoc", "../dfs.proto", "--cpp_out=.", "--proto_path=.."], check=True)

    subprocess.run(["protoc-c", "--proto_path=..", "--c_out=../c-client", "../dfs.proto"], check=True)
    subprocess.run(["protoc-c", "--proto_path=..", "--c_out=../mock-replica", "../dfs.proto"], check=True)

    subprocess.run(["make"], cwd=replica_dir, check=True) #check zeby na pewno sie uruchomilo
    subprocess.run(["make"], cwd=client_dir, check=True)
    
    replica_process = subprocess.Popen(
        ["./mock_replica"],
        cwd=replica_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    # replica_output, replica_errors = replica_process.communicate()
    
    server_process = subprocess.Popen(
        ["python3", "main_server.py"],
        cwd=server_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    
    time.sleep(2)
    # wait_for_port(8080)
    # wait_for_port(8001)
    yield replica_process, server_process

    replica_process.terminate()
    server_process.terminate()

def test_client_read(build_and_start_services):
    mock_replica_process, server_process = build_and_start_services

    client_proc = subprocess.Popen(
        ["./client"],
        cwd="../c-client",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    # Pozniej bedzie cos w stylu
    # client_proc = subprocess.Popen(
    #     ["./client", "read", "ala.txt"],
    #     cwd="c-client",
    #     stdout=subprocess.PIPE,
    #     stderr=subprocess.PIPE
    # )

    out, err = client_proc.communicate(timeout=10)
    
    out = out.decode("utf-8")
    err = err.decode("utf-8")

    assert "n_chunks: 2" in out, "[ERR] n_chunks != 2"
    assert "chunk_id: 1" in out, "[ERR] No chunk_id 1"
    assert "chunk_id: 2" in out, "[ERR] No chunk_id 2"
    assert "received: Ala ma kota" in out, "[ERR] No message from chunk 1"
    assert "received: a kot ma Ale" in out, "[ERR] No message from chunk 2"

    # assert client_proc.returncode == 0, f"Errors: {err}"