# Testing with Ubuntu 18.04 docker containers

These docker files are for easily testing specific setups locally when a travis build fails. 
When the image is built by default it will have pulled and compiled the master branch. You can either open the image in a new
container in a bash shell to change to a new branch and then recompile the code
or edit the commented section of the dockerfile to have the image build with a specific branch

## Recommended image names
- img-bucash-ubu18-linux32
- img-bucash-ubu18-linux64
- img-bucash-ubu18-windows32
- img-bucash-ubu18-windows64

## Recommended container names
- bucash-linux32
- bucash-linux64
- bucash-windows32
- bucash-windows64

## To build
`docker build --no-cache -t <image name> -f <docker file name> .`

example: 

`docker build --no-cache -t img-bucash-ubu18-linux64 -f Dockerfile.ubuntu18-linux64 .`

## To start an image in a new container in a bash shell
`docker run -it -d --name <container name> <imagename> bash`

example: 

`docker run -it -d --name bucash-linux64 img-bucash-ubu18-linux64 bash`

## To start an image in a new container in a bash shell with a mounted directory and opened port
```
docker run -p <port mapping> -v /path/to/a/local/directory:/root/.bitcoin --name <container name> <image name> bash
```

port mapping maps an internal port to an external one by internal:external

example -p 8333:1234 will map the port 8333 internal to the docker container to host machine port 1234

## To connect to the container in a bash shell
`docker exec -it <container name> bash`
