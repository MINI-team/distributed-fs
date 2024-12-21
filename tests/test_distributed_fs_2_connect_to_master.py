import subprocess
import pytest
import time
import socket
from pathlib import Path
import shlex

@pytest.fixture(scope="module")
def setup_docker_environment():
    script_dir = Path("../")
    command = "./docker-restart-detached.sh docker-compose-write.yml"
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

def test_client_output(setup_docker_environment):
    client_logs = get_logs("docker logs distributed-fs-client_container-1")
    server_logs = get_logs("docker logs distributed-fs-server_container-1")
    
    assert "Succesfully connected to 9001" in client_logs, "[ERR] client failed to connect with server"

    assert "write request detected" in server_logs, "[ERR] write request from client wasn't received by master"
    assert "fileRequestWrite->path: alphabet" in server_logs, "[ERR] master didn't receive right path"
    assert "fileRequestWrite->size: 26" in server_logs, "[ERR] master didn't receive right size"