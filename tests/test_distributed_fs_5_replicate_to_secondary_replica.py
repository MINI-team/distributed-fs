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
    sec_replica_logs = get_logs("docker logs distributed-fs-replica_container_1-1")

    assert "received chunk: abcde" in sec_replica_logs, "[ERR] secondary replica didn't receive abcde"
    assert "received chunk: fghij" in sec_replica_logs, "[ERR] secondary replica didn't receive fghij"
    assert "received chunk: klmno" in sec_replica_logs, "[ERR] secondary replica didn't receive klmno"
    assert "received chunk: pqrst" in sec_replica_logs, "[ERR] secondary replica didn't receive pqrst"
    assert "received chunk: uvwxy" in sec_replica_logs, "[ERR] secondary replica didn't receive uvwxy"
    assert "received chunk: z" in sec_replica_logs, "[ERR] secondary replica didn't receive z"
