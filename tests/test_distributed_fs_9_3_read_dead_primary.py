import subprocess
import pytest
import time
import socket
from pathlib import Path
import shlex
import hashlib

@pytest.fixture(scope="module")
def setup_docker_environment():
    script_dir = Path("../")
    command = "./docker-restart-detached.sh docker-compose.yml"
    args = shlex.split(command)

    result = subprocess.run(
        args,
        cwd=script_dir,
        capture_output=True,
        text=True,
        check=True
    )

    time.sleep(40)
    
    yield

def get_logs(command):
    args = shlex.split(command)
    container_logs = subprocess.run(
        args,
        capture_output=True,
        text=True,
        check=True,
    )
    logs = container_logs.stdout
    print("=================== Logs are ===================\n", logs)
    return logs

def kill_replica_container():
    command = "docker kill distributed-fs-replica_container_0-1"
    args = shlex.split(command)

    try:
        result = subprocess.run(
            args,
            capture_output=True,
            text=True,
            check=True
        )
        print(f"Successfully killed the container: {result.stdout}")
    except subprocess.CalledProcessError as e:
        print(f"[ERR] Failed to kill the container: {e}")

def execute_client_command():
    script_dir = Path("../build/client")
    client_commands = (
        "./client write alphabet 172.17.0.1",
        "sleep 2",
        "./client read alphabet 172.17.0.1"
    )
    # it's better to take ip from inspect
    for command in client_commands:
        args = shlex.split(command)
        result = subprocess.run(
            args,
            cwd=script_dir,
            capture_output=True,
            text=True,
            check=True
        )
    return

def md5sum(filename):
    with open(filename, 'rb') as f:
        file_data = f.read()
    return hashlib.md5(file_data).hexdigest()

def compare_md5(file1, file2):
    hash1 = md5sum(file1)
    hash2 = md5sum(file2)
    return hash1 == hash2

def test_client_output(setup_docker_environment):
    kill_replica_container()
    time.sleep(10)
    execute_client_command()

    client_file = Path("../build/client/alphabet")
    output_file = Path("../build/client/alphabet_output.txt")
    assert compare_md5(client_file, output_file), "MD5 checksum mismatch for alphabet"
    
