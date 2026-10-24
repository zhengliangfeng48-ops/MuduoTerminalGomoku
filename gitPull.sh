#!/bin/bash

while true;do
	if git pull;then 
		break
	fi
	sleep 10
done
