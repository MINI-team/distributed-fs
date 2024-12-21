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
    pr_replica_logs = get_logs("docker logs distributed-fs-replica_container_0-1")
    sec_replica_logs = get_logs("docker logs distributed-fs-replica_container_1-1")

    assert "opening file: alphabet" in client_logs, "[ERR] client didn't receive/parse the file(path)"
    assert "file size: 26 bytes" in client_logs, "[ERR] client failed in reading the file (from its disk)"
    
    assert "Succesfully connected to 9001" in client_logs, "[ERR] client failed to connect with server"

    assert "write request detected" in server_logs, "[ERR] write request from client wasn't received by master"
    assert "fileRequestWrite->path: alphabet" in server_logs, "[ERR] master didn't receive right path"
    assert "fileRequestWrite->size: 26" in server_logs, "[ERR] master didn't receive right size"

    assert "fileRequestWrite->path: alphabet" in client_logs, "[ERR] client didn't receive the filepath from server"
    assert "fileRequestWrite->size: 26" in client_logs, "[ERR] client didn't receive the right file size from server"
    assert "write: n_chunks: 6" in client_logs, "[ERR] client didn't receive the right number of chunks from server"
    
    assert "Unable to connect to 8080, trying a different replica..." not in client_logs, "[ERR] client didn't connect with (primary) replica"
    
    assert "sent abcde to replica" in client_logs, "[ERR] abcde wasn't sent"
    assert "sent fghij to replica" in client_logs, "[ERR] fghij wasn't sent"
    assert "sent klmno to replica" in client_logs, "[ERR] klmno wasn't sent"
    assert "sent pqrst to replica" in client_logs, "[ERR] pqrst wasn't sent"
    assert "sent uvwxy to replica" in client_logs, "[ERR] uvwx wasn't sent"
    assert "sent z to replica" in client_logs, "[ERR] z wasn't sent"

    assert "received chunk: abcde" in pr_replica_logs, "[ERR] primary replica didn't receive abcde"
    assert "received chunk: fghij" in pr_replica_logs, "[ERR] primary replica didn't receive fghij"
    assert "received chunk: klmno" in pr_replica_logs, "[ERR] primary replica didn't receive klmno"
    assert "received chunk: pqrst" in pr_replica_logs, "[ERR] primary replica didn't receive pqrst"
    assert "received chunk: uvwxy" in pr_replica_logs, "[ERR] primary replica didn't receive uvwxy"
    assert "received chunk: z" in pr_replica_logs, "[ERR] primary replica didn't receive z"

    assert "received chunk: abcde" in sec_replica_logs, "[ERR] secondary replica didn't receive abcde"
    assert "received chunk: fghij" in sec_replica_logs, "[ERR] secondary replica didn't receive fghij"
    assert "received chunk: klmno" in sec_replica_logs, "[ERR] secondary replica didn't receive klmno"
    assert "received chunk: pqrst" in sec_replica_logs, "[ERR] secondary replica didn't receive pqrst"
    assert "received chunk: uvwxy" in sec_replica_logs, "[ERR] secondary replica didn't receive uvwxy"
    assert "received chunk: z" in sec_replica_logs, "[ERR] secondary replica didn't receive z"
