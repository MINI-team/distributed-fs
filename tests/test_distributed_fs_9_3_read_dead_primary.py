import subprocess
import pytest
import time
import socket
from pathlib import Path
import shlex

@pytest.fixture(scope="module")
def setup_docker_environment():
    script_dir = Path("../")
    command = "./docker-restart-detached.sh docker-compose-read-dead-primary.yml"
    args = shlex.split(command)

    result = subprocess.run(
        args,
        cwd=script_dir,
        capture_output=True,
        text=True,
        check=True
    )

    time.sleep(30)
    
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
    sec_replica_logs = get_logs("docker logs distributed-fs-replica_container_1-1")
    
    assert "Unable to connect to 8080, trying a different replica..." in client_logs, "[ERR] no problem connecting to primary replica, wrong test?"
    assert "Succesfully connected to 8081" in client_logs, "[ERR] problem connecting with secondary replica"
    
    assert "sent abcde to replica" in client_logs, "[ERR] abcde wasn't sent"
    assert "sent fghij to replica" in client_logs, "[ERR] fghij wasn't sent"
    assert "sent klmno to replica" in client_logs, "[ERR] klmno wasn't sent"
    assert "sent pqrst to replica" in client_logs, "[ERR] pqrst wasn't sent"
    assert "sent uvwxy to replica" in client_logs, "[ERR] uvwx wasn't sent"
    assert "sent z to replica" in client_logs, "[ERR] z wasn't sent"

    assert "received chunk: abcde" in sec_replica_logs, "[ERR] secondary replica didn't receive abcde"
    assert "received chunk: fghij" in sec_replica_logs, "[ERR] secondary replica didn't receive fghij"
    assert "received chunk: klmno" in sec_replica_logs, "[ERR] secondary replica didn't receive klmno"
    assert "received chunk: pqrst" in sec_replica_logs, "[ERR] secondary replica didn't receive pqrst"
    assert "received chunk: uvwxy" in sec_replica_logs, "[ERR] secondary replica didn't receive uvwxy"
    assert "received chunk: z" in sec_replica_logs, "[ERR] secondary replica didn't receive z"
