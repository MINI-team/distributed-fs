import subprocess
import pytest
import time
import socket
from pathlib import Path
import shlex

@pytest.fixture(scope="module")
def setup_docker_environment():
    script_dir = Path("../")
    command = "./docker-restart-detached.sh docker-compose-2-write-read.yml"
    args = shlex.split(command)

    # result = subprocess.run(
    #     args,
    #     cwd=script_dir,
    #     capture_output=True,
    #     text=True,
    #     check=True
    # )

    time.sleep(20)
    
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

    assert "received: abcde" in client_logs, "[ERR] client didn't receive abcde"
    assert "received: fghij" in client_logs, "[ERR] client didn't receive fghij"
    assert "received: klmno" in client_logs, "[ERR] client didn't receive klmno"
    assert "received: pqrst" in client_logs, "[ERR] client didn't receive pqrst"
    assert "received: uvwxy" in client_logs, "[ERR] client didn't receive uvwxy"
    assert "received: z" in client_logs, "[ERR] client didn't receive z"

    assert "received:  Ale" in client_logs, "[ERR] client didn't receive Ale"
    assert "received: ot ma" in client_logs, "[ERR] client didn't receive ot ma"
    assert "received: a kot" in client_logs, "[ERR] client didn't receive a kot"
    assert "received: a a k" in client_logs, "[ERR] client didn't receive a a k"
    assert "received: Ala m" in client_logs, "[ERR] client didn't receive Ala m"
