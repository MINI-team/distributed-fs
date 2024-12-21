#!/bin/bash

docker compose down --volumes --remove-orphans

#docker compose -f docker-compose.yml up --build
docker compose -f $1 up --build
