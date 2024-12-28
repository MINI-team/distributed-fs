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
    
    yield

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
    client_output = execute_client_command()
    assert "opening file: alphabet" in client_output, "[ERR] error while opening file"
    assert "file size: 26 bytes" in client_output, "[ERR] error while reading file"


# def get_container_ip(container_name):
#     command = f"docker inspect -f '{{{{range .NetworkSettings.Networks}}{{{{.IPAddress}}}}{{end}}}}' {container_name}"
#     args = shlex.split(command)
#     result = subprocess.run(
#         args,
#         capture_output=True,
#         text=True,
#         check=True
#     )
#     return result.stdout.strip()