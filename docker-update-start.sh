#!/bin/sh
sudo docker-compose pull
sudo docker-compose up --force-recreate --build -d
