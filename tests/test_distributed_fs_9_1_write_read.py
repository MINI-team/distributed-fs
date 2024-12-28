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

def execute_client_command():
    script_dir = Path("../build/client")
    client_commands = (
        "./client write alphabet 172.17.0.1",
        "sleep 2",
        "./client read alphabet 172.17.0.1"
    )
    # it's better to take ip from inspect
    logs = []
    for command in client_commands:
        args = shlex.split(command)
        result = subprocess.run(
            args,
            cwd=script_dir,
            capture_output=True,
            text=True,
            check=True
        )
        logs.append(result.stdout)
    return "\n".join(logs)


def test_client_output(setup_docker_environment):
    client_logs = execute_client_command()

    assert "received: abcde" in client_logs, "[ERR] client didn't receive abcde"
    assert "received: fghij" in client_logs, "[ERR] client didn't receive fghij"
    assert "received: klmno" in client_logs, "[ERR] client didn't receive klmno"
    assert "received: pqrst" in client_logs, "[ERR] client didn't receive pqrst"
    assert "received: uvwxy" in client_logs, "[ERR] client didn't receive uvwxy"
    assert "received: z" in client_logs, "[ERR] client didn't receive z"