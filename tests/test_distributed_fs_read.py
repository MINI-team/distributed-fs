import subprocess
import pytest
import time
import socket
from pathlib import Path
import shlex

@pytest.fixture(scope="module")
def setup_docker_environment():
    script_dir = Path("../")
    result = subprocess.run(
    ["./docker-restart-detached.sh"],
    cwd=script_dir,
    capture_output=True,
    text=True,
    check=True
)
    print(result.stdout)

    time.sleep(3)
    
    yield

def test_client_output(setup_docker_environment):
    command = "docker compose logs"
    args = shlex.split(command)
    container_logs = subprocess.run(
        args,
        capture_output=True,
        text=True,
        check=True,
    )
    logs = container_logs.stdout

    assert "n_chunks: 2" in logs, "[ERR] n_chunks != 2 in client output"
    assert "chunk_id: 0" in logs, "[ERR] No chunk_id 0 in client output"
    assert "chunk_id: 1" in logs, "[ERR] No chunk_id 1 in client output"
    # assert "received: Ala ma kota" in logs, "[ERR] No message from chunk 0 in client output"
    # assert "received: a kot ma Ale" in logs, "[ERR] No message from chunk 1 in client output"
