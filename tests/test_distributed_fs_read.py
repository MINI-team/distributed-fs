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
    #print(result.stdout)

    time.sleep(3)
    
    yield

def test_client_output(setup_docker_environment):
    command = "docker compose logs"
    args = shlex.split(command)
    container_logs = subprocess.run(
        # args, # glowacz needs to use this instead of line below
        ["docker", "logs", "distributed-fs_client_container_1"],
        capture_output=True,
        text=True,
        check=True,
    )
    logs = container_logs.stdout
    print(logs)
    assert "n_chunks: 6" in logs, "[ERR] n_chunks != 2 in client output"
    assert "n_replicas: 3" in logs, "[ERR] n_replicas != 3 in client output"
    assert "sent abcde to replica" in logs, "[ERR] abcde wasn't sent"
    assert "sent fghij to replica" in logs, "[ERR] fghij wasn't sent"
    assert "sent klmno to replica" in logs, "[ERR] klmno wasn't sent"
    assert "sent pqrst to replica" in logs, "[ERR] pqrst wasn't sent"
    assert "sent uvwxy to replica" in logs, "[ERR] uvwx wasn't sent"
    assert "sent z to replica" in logs, "[ERR] z wasn't sent"
