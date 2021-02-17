set -e
docker build --no-cache -t bchunlimited/gitlabci:ubuntu18.04 .
docker push bchunlimited/gitlabci:ubuntu18.04
