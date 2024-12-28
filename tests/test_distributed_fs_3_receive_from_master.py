import subprocess
import pytest
import time
import socket
from pathlib import Path
import shlex

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

    time.sleep(6)
    
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

def execute_client_command():
    script_dir = Path("../build/client")
    client_command = "./client write alphabet 172.17.0.1"
    # it's better to take ip from inspect
    args = shlex.split(client_command)
    
    result = subprocess.run(
        args,
        cwd=script_dir,
        capture_output=True,
        text=True,
        check=True
    )
    return result.stdout

def test_client_output(setup_docker_environment):
    client_logs = execute_client_command()
    server_logs = get_logs("docker logs distributed-fs-server_container-1")

    assert "fileRequestWrite->path: alphabet" in client_logs, "[ERR] client didn't receive the filepath from server"
    assert "fileRequestWrite->size: 26" in client_logs, "[ERR] client didn't receive the right file size from server"
    assert "write: n_chunks: 6" in client_logs, "[ERR] client didn't receive the right number of chunks from server"